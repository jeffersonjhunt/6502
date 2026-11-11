#!/bin/sh
# Deploys (or updates) the OO-1's builder stack, builder-oo1.yaml, with the admin profile: the image
# repository builder/oo1, the instance role, the security group and the launch template
# (tools/aws/README.md). Needs builder-base, whose VPC its instances run in.
#
#   sh tools/aws/deploy-oo1.sh [INSTANCE_TYPE]      # c8a.4xlarge by default; arm64 or x86_64
set -eu
type=${1:-c8a.4xlarge}
here=$(cd "$(dirname "$0")" && pwd)
status=$(aws cloudformation describe-stacks --stack-name builder-oo1 --query 'Stacks[0].StackStatus' --output text 2>/dev/null || echo NONE)
# Amazon Linux 2023 for the type's architecture (an instance builds the image for its own).
arch=$(aws ec2 describe-instance-types --instance-types "$type" --query 'InstanceTypes[0].ProcessorInfo.SupportedArchitectures' --output text)
case $arch in *arm64*) arch=arm64 ;; *x86_64*) arch=x86_64 ;; *) echo "deploy-oo1: $type: architecture $arch?" >&2; exit 1 ;; esac
echo "deploy-oo1: $type ($arch), stack $status"
if [ "$status" = ROLLBACK_COMPLETE ]; then # a first deploy that failed: nothing of it is kept, so start again
    echo "deploy-oo1: deleting the rolled-back stack"
    aws cloudformation delete-stack --stack-name builder-oo1
    aws cloudformation wait stack-delete-complete --stack-name builder-oo1
fi
aws cloudformation deploy --stack-name builder-oo1 --template-file "$here/builder-oo1.yaml" \
    --parameter-overrides "InstanceType=$type" "ImageId=/aws/service/ami-amazon-linux-latest/al2023-ami-kernel-default-$arch" \
    --capabilities CAPABILITY_NAMED_IAM --tags builder=true project=oo1
aws cloudformation describe-stacks --stack-name builder-oo1 --query 'Stacks[0].Outputs[].[OutputKey,OutputValue]' --output text
