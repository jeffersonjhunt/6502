#!/bin/bash
# The OO-1's protected fixtures, on the builder's bucket for the cloud full tier (tools/aws/README.md):
# the owner's SID tunes, Apple IIe ROMs, disk images and ROM images, which are never in git and never
# printed. They go up as one archive, named by a hash of their paths and contents, and only when that
# hash isn't the one the bucket has: oo1/fixtures/HASH.tar.zst, and oo1/fixtures/current naming it.
# The public fixtures (Dormann's, Harte's) aren't sent: the instance fetches them (fetch_fixtures.py).
#
#   tools/aws/oo1-fixtures.sh            # upload if changed
#   tools/aws/oo1-fixtures.sh --check    # say whether the bucket's are current; upload nothing
#
# Uses the builder profile (AWS_PROFILE to change it). Prints counts and hashes only.
set -euo pipefail
check=${1:-}
profile=${AWS_PROFILE:-builder}
root=$(cd "$(dirname "$0")/../.." && pwd)
fx="$root/emulator/test/host/fixtures"
dirs=(SIDs apple2e disks rom_images)
aws() { command aws --profile "$profile" "$@"; }
account=$(aws sts get-caller-identity --query Account --output text)
bucket="builder-$account-$(command aws configure get region --profile "$profile" || echo us-east-1)"
prefix="s3://$bucket/oo1/fixtures"

for d in "${dirs[@]}"; do
    [ -d "$fx/$d" ] || { echo "oo1-fixtures: no fixtures/$d here: nothing to send" >&2; exit 1; }
done
# The hash: each file's path (under fixtures/, sorted) and its contents' SHA-256.
hash=$(cd "$fx" && python3 -I -c '
import hashlib, os, sys
h, n, size = hashlib.sha256(), 0, 0
for top in sys.argv[1:]:
    for dirpath, dirnames, files in os.walk(top, followlinks=True):
        dirnames.sort()
        for f in sorted(files):
            p = os.path.join(dirpath, f)
            c = hashlib.sha256()
            with open(p, "rb") as fh:
                for b in iter(lambda: fh.read(1 << 20), b""):
                    c.update(b)
                    size += len(b)
            h.update(p.encode() + b"\0" + c.digest())
            n += 1
if n == 0:
    sys.exit("oo1-fixtures: no files: nothing to send")
print(h.hexdigest()[:32], n, size)
' "${dirs[@]}")
read -r id files bytes <<<"$hash"
have=$(aws s3 cp "$prefix/current" - 2>/dev/null || true)
echo "oo1-fixtures: here $id ($files files, $((bytes / 1048576)) MB); the bucket's ${have:-none}"
if [ "$have" = "$id" ]; then
    echo "oo1-fixtures: current: nothing sent"
    exit 0
fi
if [ "$check" = --check ]; then
    echo "oo1-fixtures: changed: run without --check to send them"
    exit 2
fi
echo "oo1-fixtures: sending $id"
tar -C "$fx" -chf - --sort=name "${dirs[@]}" | zstd -q -T0 -10 |
    aws s3 cp - "$prefix/$id.tar.zst" --only-show-errors --expected-size "$bytes"
size=$(aws s3api head-object --bucket "$bucket" --key "oo1/fixtures/$id.tar.zst" --query ContentLength --output text)
printf '%s' "$id" | aws s3 cp - "$prefix/current" --only-show-errors
if [ -n "$have" ]; then
    aws s3 rm "$prefix/$have.tar.zst" --only-show-errors || true
fi
echo "oo1-fixtures: sent $id: $((size / 1048576)) MB compressed; current names it"
