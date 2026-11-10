#!/bin/sh
# Deploys (or updates) the OO-1's builder stack, builder-oo1.yaml, with the admin profile: the image
# repository builder/oo1, the instance role, the security group and the launch template
# (tools/aws/README.md). Needs builder-base, whose VPC its instances run in.
#
#   sh tools/aws/deploy-oo1.sh [INSTANCE_TYPE]      # c7g.4xlarge by default (arm64 only: the image is)
set -eu
type=${1:-c7g.4xlarge}
here=$(cd "$(dirname "$0")" && pwd)
echo "deploy-oo1: $type"
aws cloudformation deploy --stack-name builder-oo1 --template-file "$here/builder-oo1.yaml" \
    --parameter-overrides "InstanceType=$type" \
    --capabilities CAPABILITY_NAMED_IAM --tags builder=true project=oo1
aws cloudformation describe-stacks --stack-name builder-oo1 --query 'Stacks[0].Outputs[].[OutputKey,OutputValue]' --output text
