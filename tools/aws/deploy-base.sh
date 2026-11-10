#!/bin/sh
# Deploys (or updates) the builder's base stack, builder-base.yaml, with the admin profile: the bucket,
# its key, the builder role and the budget (tools/aws/README.md).
#
#   sh tools/aws/deploy-base.sh EMAIL [BUDGET_USD]
#
# The operator, the IAM user the builder role trusts, is whoever runs this (sts get-caller-identity).
# A first deploy that failed leaves the stack in ROLLBACK_COMPLETE and the bucket kept (its
# DeletionPolicy is Retain), encrypted with a key the rollback deleted: in that state, and only if
# the bucket is empty, this deletes both first, then deploys again.
set -eu
email=${1:?usage: deploy-base.sh EMAIL [BUDGET_USD]}
budget=${2:-100}
here=$(cd "$(dirname "$0")" && pwd)
account=$(aws sts get-caller-identity --query Account --output text)
operator=$(aws sts get-caller-identity --query Arn --output text)
region=$(aws configure get region || echo us-east-1)
bucket="builder-$account-$region"
status=$(aws cloudformation describe-stacks --stack-name builder-base --query 'Stacks[0].StackStatus' --output text 2>/dev/null || echo NONE)
echo "deploy-base: operator $operator, stack $status"
if [ "$status" = ROLLBACK_COMPLETE ]; then
    if aws s3api head-bucket --bucket "$bucket" 2>/dev/null; then
        n=$(aws s3api list-object-versions --bucket "$bucket" --max-keys 1 --query 'length(Versions || `[]`)' --output text)
        if [ "$n" != 0 ]; then
            echo "deploy-base: $bucket isn't empty: not deleting it; look before going on" >&2
            exit 1
        fi
        echo "deploy-base: deleting the kept, empty $bucket"
        aws s3api delete-bucket --bucket "$bucket"
    fi
    echo "deploy-base: deleting the rolled-back stack"
    aws cloudformation delete-stack --stack-name builder-base
    aws cloudformation wait stack-delete-complete --stack-name builder-base
fi
aws cloudformation deploy --stack-name builder-base --template-file "$here/builder-base.yaml" \
    --parameter-overrides "Operator=$operator" "BudgetEmail=$email" "BudgetUsd=$budget" \
    --capabilities CAPABILITY_NAMED_IAM --tags builder=true
aws cloudformation describe-stacks --stack-name builder-base --query 'Stacks[0].Outputs[].[OutputKey,OutputValue]' --output text
