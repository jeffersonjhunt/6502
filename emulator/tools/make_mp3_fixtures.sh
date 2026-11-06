#!/bin/sh
# Makes the MP3 test files (emulator/test/host/mp3/, test_sound_mp3.cpp reads them): our own synthetic signal,
# made here by Python, encoded by LAME. No third-party audio. Needs Docker; everything runs in a throwaway
# debian:bookworm, pinned by digest, with LAME and Python pinned by version, so the same script makes the same
# files; it checks them against the sha256s recorded below and fails if they differ.
#
#   emulator/tools/make_mp3_fixtures.sh
#
# The signal, 0.6 s: on the left a chirp from 200 Hz to 4 kHz over a 440 Hz tone, on the right a 660 Hz
# tone over seeded noise, each at -9 dBFS or so; mono files get the two summed. Written as 16-bit WAV at the
# file's rate and encoded:
#
#   cbr320_stereo_48k.mp3   MPEG-1, 320 kbps CBR, stereo, 48 kHz        (high bitrate)
#   cbr128_joint_44k.mp3    MPEG-1, 128 kbps CBR, joint stereo, 44.1 kHz (the junk and padding tests')
#   cbr32_mono_22k.mp3      MPEG-2, 32 kbps CBR, mono, 22.05 kHz         (low bitrate)
#   cbr8_mono_8k.mp3        MPEG-2.5, 8 kbps CBR, mono, 8 kHz            (the lowest)
#   vbr0_stereo_44k.mp3     MPEG-1, VBR -V 0, joint stereo, 44.1 kHz     (VBR, high)
#   vbr9_mono_22k.mp3       MPEG-2, VBR -V 9, mono, 22.05 kHz            (VBR, low)
#
# Each is about 1 KB to 24 KB. Hostpath: from inside a container, set HOSTPATH as for web_build.sh.
set -eu
image=debian:bookworm@sha256:854dd8ab51d62866dbd8f5431d15ce77b12983a2a4a1e1fe43a2395dc7ef9f61
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="$root/emulator/test/host/mp3"
map=${HOSTPATH:-hostpath}
mkdir -p "$out"
docker run --rm -i -v "$($map "$out"):/out" "$image" sh -eu <<'INNER'
apt-get -qq update >/dev/null
apt-get -qq install -y lame=3.100-6 python3=3.11.2-1+b1 >/dev/null
echo "debian $(cat /etc/debian_version); the packages:"
dpkg-query -W -f '  ${Package} ${Version}\n' lame python3
cd /tmp
cat > signal.py <<'PY'
import math, random, struct, sys, wave
rate, channels, name = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
rnd = random.Random(1)
n = int(rate * 0.6)
frames = bytearray()
phase = 0.0
for i in range(n):
    t = i / rate
    f = 200 + (4000 - 200) * t / 0.6
    phase += 2 * math.pi * f / rate
    left = 0.2 * math.sin(phase) + 0.15 * math.sin(2 * math.pi * 440 * t)
    right = 0.25 * math.sin(2 * math.pi * 660 * t) + 0.08 * (rnd.random() * 2 - 1)
    side = [left, right] if channels == 2 else [(left + right) / 2]
    for v in side:
        frames += struct.pack('<h', max(-32768, min(32767, round(v * 32767))))
with wave.open(name, 'wb') as w:
    w.setnchannels(channels)
    w.setsampwidth(2)
    w.setframerate(rate)
    w.writeframes(bytes(frames))
PY
enc() { # name rate channels lame-options...
    name=$1 rate=$2 ch=$3
    shift 3
    python3 signal.py "$rate" "$ch" in.wav
    lame --quiet --noreplaygain "$@" in.wav "/out/$name"
}
enc cbr320_stereo_48k.mp3 48000 2 -b 320 --cbr -m s
enc cbr128_joint_44k.mp3 44100 2 -b 128 --cbr -m j
enc cbr32_mono_22k.mp3 22050 1 -b 32 --cbr -m m
enc cbr8_mono_8k.mp3 8000 1 -b 8 --cbr -m m
enc vbr0_stereo_44k.mp3 44100 2 -V 0 -m j
enc vbr9_mono_22k.mp3 22050 1 -V 9 -m m
cd /out
ls -l *.mp3
# The files these versions made when the script was written: a rerun must make the same bytes.
sha256sum -c <<'SUMS'
70ccbc742c0f64b65e4b40ee037d29e9a38110f9337c3b41aef5f0760b262e78  cbr128_joint_44k.mp3
86399e821f6e4bc7d1d5c7c113cd309c0961a045f91b61d3d284c5cf4b92ab5b  cbr320_stereo_48k.mp3
9ef7eb6494f0a2754bfd3894f6483d19148dd4b91596de05d2196845267b5e72  cbr32_mono_22k.mp3
6dee47a4fa6b92bb927e78c1c4c010dcaa22edcd835e9bbd46ec583ae6aca80e  cbr8_mono_8k.mp3
1da9e1aa63bae092b156878ea3c16d1f8fabf366b69c65ba95ebd73829f1e731  vbr0_stereo_44k.mp3
628fdfe90542132116328ec876677ddf32eaaacc6b42696812f93ef09228732c  vbr9_mono_22k.mp3
SUMS
INNER
