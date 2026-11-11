#!/bin/sh
# Tears down what the retired AWS builder left in account 521147433280 (hw/testing.md says why it was
# retired). Run it once, with the admin profile (the one that deployed the stacks), then delete this
# script in a commit of its own.
#
#   sh tools/aws-teardown.sh          # lists what is there, deletes nothing
#   sh tools/aws-teardown.sh --yes    # deletes it, then checks nothing tagged builder=true is left
#
# In order: any builder-oo1 instance still running; the builder/oo1 ECR repository and its images; the
# builder-oo1 stack (instance role and profile, security group, launch template); the bucket
# builder-521147433280-us-east-1 (the fixtures, run logs and sources), emptied and deleted, as its
# DeletionPolicy is Retain; the builder-base stack (the builder role, the KMS key, the VPC with its
# subnets, gateway, routes and S3 endpoint, the budget). CloudFormation schedules the KMS key's
# deletion (30 days, the default; a key pending deletion costs nothing). The CloudWatch metrics in the
# namespace `builder` need nothing: AWS drops custom metrics that stop arriving.
set -eu
account=521147433280
region=${AWS_REGION:-$(aws configure get region 2>/dev/null || echo us-east-1)}
bucket="builder-$account-$region"
repo=builder/oo1
yes=; [ "${1:-}" = --yes ] && yes=1

say() { echo "aws-teardown: $*"; }
got=$(aws sts get-caller-identity --query Account --output text)
[ "$got" = "$account" ] || { say "this profile is account $got, not $account: stopping" >&2; exit 1; }
say "account $account, region $region, as $(aws sts get-caller-identity --query Arn --output text)"

stack() { aws cloudformation describe-stacks --stack-name "$1" --query 'Stacks[0].StackStatus' --output text 2>/dev/null || echo none; }
instances=$(aws ec2 describe-instances --region "$region" \
    --filters Name=tag:builder,Values=true Name=instance-state-name,Values=pending,running,stopping,stopped \
    --query 'Reservations[].Instances[].InstanceId' --output text)
images=$(aws ecr list-images --region "$region" --repository-name "$repo" --query 'length(imageIds)' --output text 2>/dev/null || echo none)
if aws s3api head-bucket --bucket "$bucket" >/dev/null 2>&1; then
    objects=$(aws s3 ls "s3://$bucket" --recursive --summarize | awk '/Total Objects/{print $3}')
else
    objects=none
fi
say "instances running: ${instances:-none}"
say "ECR $repo: ${images} images"
say "stack builder-oo1: $(stack builder-oo1)"
say "bucket $bucket: $objects objects"
say "stack builder-base: $(stack builder-base)"
[ -n "$yes" ] || { say "dry run: sh tools/aws-teardown.sh --yes deletes all of the above"; exit 0; }

if [ -n "$instances" ]; then
    say "terminating $instances"
    aws ec2 terminate-instances --region "$region" --instance-ids $instances >/dev/null
    aws ec2 wait instance-terminated --region "$region" --instance-ids $instances
fi
if [ "$images" != none ]; then
    say "deleting ECR $repo and its images"
    aws ecr delete-repository --region "$region" --repository-name "$repo" --force >/dev/null
fi
if [ "$(stack builder-oo1)" != none ]; then
    say "deleting stack builder-oo1"
    aws cloudformation delete-stack --stack-name builder-oo1
    aws cloudformation wait stack-delete-complete --stack-name builder-oo1
fi
if [ "$objects" != none ]; then
    say "emptying $bucket"
    aws s3 rm "s3://$bucket" --recursive --only-show-errors
    # Any older versions and delete markers too (the bucket isn't versioned, but a bucket with any left
    # can't be deleted).
    while :; do
        page=$(aws s3api list-object-versions --bucket "$bucket" --max-items 1000 --output json \
            --query '{Objects: [Versions, DeleteMarkers][][].{Key: Key, VersionId: VersionId}}')
        echo "$page" | grep -q '"Key"' || break
        aws s3api delete-objects --bucket "$bucket" --delete "$page" >/dev/null
    done
    say "deleting $bucket"
    aws s3api delete-bucket --bucket "$bucket"
fi
if [ "$(stack builder-base)" != none ]; then
    say "deleting stack builder-base"
    aws cloudformation delete-stack --stack-name builder-base
    aws cloudformation wait stack-delete-complete --stack-name builder-base
fi

# What is left with the builder tag (a resource pending deletion, the KMS key, is expected).
left=$(aws resourcegroupstaggingapi get-resources --region "$region" --tag-filters Key=builder,Values=true \
    --query 'ResourceTagMappingList[].ResourceARN' --output text)
for arn in $left; do
    case $arn in
    arn:aws:kms:*) say "KMS key pending deletion (expected): $arn" ;;
    *) say "STILL THERE: $arn" ;;
    esac
done
say "done. Left for you: the [profile builder] section of ~/.aws/config (it names the deleted role),"
say "and, if you activated it, the builder cost allocation tag (Billing, Cost allocation tags)."
