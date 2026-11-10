#!/bin/bash
# tools/check.sh on a throw-away builder instance (tools/aws/README.md): the full tier by default, on a
# commit, away from this host's other work.
#
#   tools/cloud-full.sh [REF]            # check.sh full on REF (HEAD by default); waits, prints the summary
#   tools/cloud-full.sh --quick [REF]    # the quick tier instead
#   tools/cloud-full.sh --list           # the builder-oo1 instances running
#   tools/cloud-full.sh --kill ID        # terminate one
#   tools/cloud-full.sh --wait RUN       # wait for a run already started, and fetch it
#
# Options: --on-demand (not spot), --minutes N (the instance's watchdog, 180 by default), --out DIR
# (where the logs come back; $TMPDIR/cloud-full/RUN by default), --no-wait.
#
# Each run: the image builder/oo1:HASH (HASH of tools/ci/Dockerfile) built and pushed if ECR lacks it;
# the protected fixtures sent if they changed (tools/aws/oo1-fixtures.sh); REF's tree, by git archive,
# to oo1/src/RUN.tar.gz; an instance from the builder-oo1 launch template, its run in its tags. It runs
# check.sh, puts run.log, check.out, logs/ and exit under oo1/runs/RUN/, and powers off, which
# terminates it. Uses the builder profile (AWS_PROFILE to change it).
set -euo pipefail
profile=${AWS_PROFILE:-builder}
aws() { command aws --profile "$profile" "$@"; }
repo=$(cd "$(dirname "$0")/.." && pwd)
tier=full market=spot minutes=180 out= wait=1 ref=HEAD action=run arg=
while [ $# -gt 0 ]; do
    case $1 in
    --quick) tier=quick ;;
    --on-demand) market=on-demand ;;
    --minutes) minutes=$2; shift ;;
    --out) out=$2; shift ;;
    --no-wait) wait= ;;
    --list) action=list ;;
    --kill) action=kill; arg=$2; shift ;;
    --wait) action=wait; arg=$2; shift ;;
    -h | --help) sed -n '2,20p' "$0"; exit 0 ;;
    -*) echo "cloud-full: $1?" >&2; exit 2 ;;
    *) ref=$1 ;;
    esac
    shift
done
region=$(command aws configure get region --profile "$profile" || echo us-east-1)
account=$(aws sts get-caller-identity --query Account --output text)
bucket="builder-$account-$region"
registry="$account.dkr.ecr.$region.amazonaws.com"
repository=builder/oo1

list() {
    aws ec2 describe-instances --filters Name=tag:project,Values=oo1 Name=tag:builder,Values=true \
        Name=instance-state-name,Values=pending,running,stopping \
        --query 'Reservations[].Instances[].[InstanceId,InstanceType,InstanceLifecycle||`on-demand`,State.Name,LaunchTime,Tags[?Key==`run`]|[0].Value]' \
        --output text
}

fetch() { # RUN: wait for its exit, then bring its logs here
    local run=$1 dir=${out:-${TMPDIR:-/tmp}/cloud-full/$1} seen= last= t0
    t0=$(date +%s)
    echo "cloud-full: waiting for $run (s3://$bucket/oo1/runs/$run/)"
    while ! aws s3 cp "s3://$bucket/oo1/runs/$run/exit" - >/dev/null 2>&1; do
        last=$(aws s3 cp "s3://$bucket/oo1/runs/$run/run.log" - 2>/dev/null | tail -1 || true)
        if [ -n "$last" ] && [ "$last" != "$seen" ]; then echo "  $last"; seen=$last; fi
        if [ $(( $(date +%s) - t0 )) -gt $(( (minutes + 15) * 60 )) ]; then
            echo "cloud-full: no exit after $((minutes + 15)) min: look with --list" >&2
            return 1
        fi
        sleep 60
    done
    mkdir -p "$dir"
    aws s3 cp "s3://$bucket/oo1/runs/$run/" "$dir" --recursive --only-show-errors
    echo "cloud-full: $(cat "$dir/exit"); logs in $dir"
    [ -f "$dir/logs/summary" ] && cat "$dir/logs/summary"
    tail -4 "$dir/check.out" 2>/dev/null || true
    grep -q 'exit 0$' "$dir/exit"
}

case $action in
list) list; exit 0 ;;
kill) aws ec2 terminate-instances --instance-ids "$arg" --query 'TerminatingInstances[].CurrentState.Name' --output text; exit 0 ;;
wait) fetch "$arg"; exit $? ;;
esac

sha=$(git -C "$repo" rev-parse --verify "$ref^{commit}")
run="$(date -u +%Y%m%dT%H%M%SZ)-${sha:0:7}"

# The image, by its Dockerfile's hash.
image=$(sha256sum "$repo/tools/ci/Dockerfile" | cut -c1-12)
if ! aws ecr describe-images --repository-name "$repository" --image-ids "imageTag=$image" >/dev/null 2>&1; then
    echo "cloud-full: building and pushing $repository:$image"
    docker build -q --platform linux/arm64 -t "$repository:$image" "$repo/tools/ci" >/dev/null
    aws ecr get-login-password | docker login -u AWS --password-stdin "$registry" >/dev/null
    docker tag "$repository:$image" "$registry/$repository:$image"
    docker push -q "$registry/$repository:$image" >/dev/null
fi

# The fixtures, if they changed (the owner's rule: sent again only on a change).
AWS_PROFILE=$profile "$repo/tools/aws/oo1-fixtures.sh"

# The source: the commit's tree, as git has it.
key="oo1/src/$run.tar.gz"
git -C "$repo" archive --format=tar.gz "$sha" | aws s3 cp - "s3://$bucket/$key" --only-show-errors
aws s3api put-object-tagging --bucket "$bucket" --key "$key" --tagging 'TagSet=[{Key=kind,Value=src}]'

tags="{Key=builder,Value=true},{Key=project,Value=oo1},{Key=Name,Value=builder-oo1-$run},{Key=run,Value=$run},{Key=src,Value=$key},{Key=image,Value=$image},{Key=tier,Value=$tier},{Key=minutes,Value=$minutes},{Key=sha,Value=$sha}"
launch() { # $1: spot or on-demand
    local opts=()
    [ "$1" = spot ] && opts=(--instance-market-options 'MarketType=spot,SpotOptions={SpotInstanceType=one-time,InstanceInterruptionBehavior=terminate}')
    aws ec2 run-instances --launch-template LaunchTemplateName=builder-oo1 "${opts[@]}" \
        --tag-specifications "ResourceType=instance,Tags=[$tags]" "ResourceType=volume,Tags=[{Key=builder,Value=true},{Key=project,Value=oo1}]" \
        --query 'Instances[0].[InstanceId,InstanceType]' --output text
}
if ! got=$(launch "$market" 2>&1); then
    if [ "$market" = spot ] && grep -qE 'InsufficientInstanceCapacity|SpotMaxPriceTooLow|MaxSpotInstanceCountExceeded' <<<"$got"; then
        echo "cloud-full: no spot capacity ($(grep -oE '[A-Za-z]+Capacity|SpotMax[A-Za-z]+' <<<"$got" | head -1)): on-demand"
        got=$(launch on-demand)
    else
        echo "$got" >&2
        exit 1
    fi
fi
echo "cloud-full: run $run: ${sha:0:7}, check.sh $tier, $got ($market), image $image"
[ -n "$wait" ] || { echo "cloud-full: tools/cloud-full.sh --wait $run"; exit 0; }
fetch "$run"
