#!/bin/sh
# GW-BASIC's oracle (hw/oodos.md, "The plan, phase 7": testing; question 2): PC-BASIC, in a throwaway
# container from pinned images only, never installed on this machine. Run by hand, when a test program
# changes; its outputs are committed as expected text, never PC-BASIC itself (GPLv3).
#
#   sh oodos/gwbasic/tools/oracle.sh DIR NAME...
#
# For each DIR/NAME.bas: PC-BASIC runs it (-n: no interface; -q: quit at the end) and its output becomes
# DIR/NAME.out. If DIR/NAME.in is there, it is what is typed: the lines INPUT reads, each ended by a CR, on
# PC-BASIC's standard input (its --input option ends the run before the last lines are read), and the output
# is what PC-BASIC writes to its standard output, the lines typed shown as typed. The program goes in, and
# only the output comes out, by docker cp (the typed lines and the output by docker exec's own streams); the
# container is removed.
set -eu
IMAGE=python@sha256:5e2bdb1b0d542d5039cffb8181c65930093949194a65c365dfb1b0fe13a4fa5b  # python:3.12-slim
WHEEL=pcbasic-2.0.7-py2.py3-none-any.whl
WHEEL_SHA256=586fd172fbe08cb1f4a05c980177abba3e983229c538b9e531f958719a4f90e7
dir=$1
shift
name=gworacle-$$
docker create --name "$name" "$IMAGE" sleep 3600 >/dev/null
trap 'docker rm -f "$name" >/dev/null' EXIT
docker start "$name" >/dev/null
docker exec "$name" sh -c "pip download -q --no-deps pcbasic==2.0.7 -d /w 2>/dev/null &&
    echo '$WHEEL_SHA256  /w/$WHEEL' | sha256sum -c - >/dev/null &&
    pip install -q --no-deps /w/$WHEEL 2>/dev/null && mkdir /work"
for n in "$@"; do
    docker cp "$dir/$n.bas" "$name:/work/$n.bas"
    if [ -f "$dir/$n.in" ]; then
        docker exec -i -w /work "$name" timeout 120 pcbasic -n "$n.bas" -q <"$dir/$n.in" >"$dir/$n.out"
    else
        docker exec -w /work "$name" timeout 120 pcbasic -n "$n.bas" -q --output="$n.out"
        docker cp "$name:/work/$n.out" "$dir/$n.out"
    fi
done
