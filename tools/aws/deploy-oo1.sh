#!/bin/sh
# Deploys (or updates) the OO-1's builder stack, builder-oo1.yaml, with the admin profile: the image
# repository builder/oo1, the instance role, the security group and the launch template
# (tools/aws/README.md). Needs builder-base. The VPC is the account's default one.
#
#   sh tools/aws/deploy-oo1.sh [INSTANCE_TYPE]      # c7g.4xlarge by default (arm64 only: the image is)
set -eu
type=${1:-c7g.4xlarge}
here=$(cd "$(dirname "$0")" && pwd)
vpc=$(aws ec2 describe-vpcs --filters Name=is-default,Values=true --query 'Vpcs[0].VpcId' --output text)
[ "$vpc" != None ] || { echo "deploy-oo1: no default VPC" >&2; exit 1; }
echo "deploy-oo1: $type in $vpc"
aws cloudformation deploy --stack-name builder-oo1 --template-file "$here/builder-oo1.yaml" \
    --parameter-overrides "InstanceType=$type" "VpcId=$vpc" \
    --capabilities CAPABILITY_NAMED_IAM --tags builder=true project=oo1
aws cloudformation describe-stacks --stack-name builder-oo1 --query 'Stacks[0].Outputs[].[OutputKey,OutputValue]' --output text
