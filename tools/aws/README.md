# The builder: heavy jobs on throw-away EC2 instances

The owner's pattern (2026-11-10) for work too heavy for the shared development host: the OO-1's full test
tier first, other projects later. A job gets an instance of its own, runs, puts its results in S3 and
powers off, which terminates it. Nothing listens on it and nothing of it is left but its logs.

## The parts

| What | Where | Made by |
|---|---|---|
| Bucket `builder-ACCOUNT-REGION` (SSE-KMS `alias/builder`, no public access, TLS only), the `builder` role, the monthly budget, the builder's VPC (10.250.0.0/16: three public subnets, an internet gateway, no NAT, an S3 gateway endpoint) | [builder-base.yaml](builder-base.yaml) | `sh tools/aws/deploy-base.sh EMAIL [USD]`, admin profile |
| The OO-1's image repository `builder/oo1`, instance role `builder/builder-oo1-instance`, a security group with no inbound rule, launch template `builder-oo1` | [builder-oo1.yaml](builder-oo1.yaml) | `sh tools/aws/deploy-oo1.sh [TYPE]` (c8a.4xlarge by default; it picks the OS for the type's architecture), admin profile |
| The image `builder/oo1:HASH-ARCH`: the host's toolchain (Debian 13, gcc 14, cmake 3.31, node 20, cc65 2.19, Playwright's browsers) | [../ci/Dockerfile](../ci/Dockerfile) | the instance, when ECR lacks it (a Dockerfile change) or with `--rebuild-image` (package updates) |
| The protected fixtures (SIDs, Apple IIe ROMs, disks, ROM images): one archive named by a content hash | `oo1/fixtures/` | [oo1-fixtures.sh](oo1-fixtures.sh), only when they change |
| A run's source (`git archive`), logs and exit | `oo1/src/RUN.tar.gz` (7 days), `oo1/runs/RUN/` (30 days) | `tools/cloud-full.sh` |

The stacks are made and changed with the admin profile only: they create IAM roles. Day-to-day runs use
the `builder` profile, which assumes the `builder` role (`~/.aws/config`: `role_arn`, `source_profile =
default`). That role launches instances only from a launch template and tagged `builder=true`, ends only
those, reads and writes the bucket, pushes and pulls `builder/*` images, and passes only `role/builder/*`
roles to EC2; it can't change IAM or a stack.

## A run

```
tools/cloud-full.sh                # the full tier on HEAD; waits and prints the summary
tools/cloud-full.sh --quick REF    # the quick tier on REF
tools/cloud-full.sh --list         # what is running
tools/cloud-full.sh --kill i-...   # end one
tools/cloud-full.sh --rebuild-image  # the image built again on the instance (package updates)
tools/cloud-full.sh --jobs 12 REF   # check.sh --jobs 12 (16, the instance's cores, by default)
```

The instance (Amazon Linux 2023, x86_64) reads its run from its tags, sets a watchdog (`--minutes`, 180 by
default), fetches the source and the fixtures, pulls the image (or builds and pushes it, the first run
after a Dockerfile change: a few minutes, on the instance's own architecture) and runs `tools/check.sh` in it, with the
instance's Docker socket and the checkout at the same path inside as out, so the WebAssembly build's and
the interop stage's containers mount it. The public fixtures (Dormann's, Harte's) are fetched there.
Spot by default, in each of the builder's three zones in turn (a zone without the type is passed over);
on-demand if none has spot capacity, or with `--on-demand`. The instance puts `check.out` in S3 every 5
minutes, and its logs at once when spot gives notice that it is taking the instance back; `cloud-full.sh`
sees an instance that ended without an exit, fetches what it left and fails, rather than waiting out the
watchdog. `check.sh` always gets `--jobs`: a commit whose `check.sh` doesn't take it (one cut from main
before the builder) is refused before anything is sent, since its fixed 4 would use a quarter of the
instance; merge main into it first. The account has no default VPC, and its own VPC is left alone: the builder's is its own.
Spot needs EC2's service-linked role, made once for the account with the admin profile:
`aws iam create-service-linked-role --aws-service-name spot.amazonaws.com`; without it, runs go on-demand.

**Looking in:** Session Manager (`aws ssm start-session --target i-...`), with the admin profile; there
is no SSH key and no inbound port.

**Its metrics:** the CloudWatch agent puts memory (`mem_used_percent`, `mem_used`, `mem_available`), the
disk (`disk_used_percent`) and CPU (`cpu_usage_active`) each minute in the namespace `builder`, by
`InstanceId` and `run`; they stay in CloudWatch 15 days at that resolution. A run's memory, for one:

```
aws cloudwatch get-metric-statistics --namespace builder --metric-name mem_used_percent \
  --dimensions Name=InstanceId,Value=i-... Name=run,Value=RUN \
  --start-time ... --end-time ... --period 60 --statistics Maximum --output table
```

## Cost

c8a.4xlarge (AMD EPYC Turin, 16 cores without SMT, 32 GB): $0.86 an hour on-demand in us-east-1, about
$0.26 as spot (2026-11-10). A full tier's length is its longest sanitizer suite's (the Disk II suites),
which runs on one core, so the fastest core wins: c7g.4xlarge (Graviton3, $0.58) ran those about twice as
long as this host does. The Intel types' 16 vCPUs are 8 cores with two threads each. The budget `builder-monthly` ($100) counts what is tagged `builder=true`, once
`builder` is activated as a cost allocation tag (Billing, Cost allocation tags); it mails at 80% spent and
at 100% forecast.
