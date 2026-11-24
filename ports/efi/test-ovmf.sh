#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

app=$(realpath "${1:?usage: test-ovmf.sh SPEECH_EFI TEST_DIRECTORY [RUNTIME_TEXT]}")
out=${2:?usage: test-ovmf.sh SPEECH_EFI TEST_DIRECTORY [RUNTIME_TEXT]}
text=${3:-Boot option 42 is ready. The firmware version is 3.7.}
ovmf=${OVMF_DIRECTORY:-/usr/share/OVMF}
mkdir -p "$out"
out=$(realpath "$out")
script=$(realpath "$0")
if [ -n "${DRIVER_EFI:-}" ]; then
	driver=$(realpath "$DRIVER_EFI")
fi
cd "$out"
image="$out/speech.img"

truncate -s 32M "$image"
mformat -i "$image" ::
mmd -i "$image" ::/EFI ::/EFI/BOOT
mcopy -i "$image" "$app" ::/EFI/BOOT/BOOTX64.EFI
printf '%s' "$text" > "$out/TEXT.TXT"
mcopy -i "$image" "$out/TEXT.TXT" ::/TEXT.TXT
expected='Speech prototype returned 0'
if [ -n "${DRIVER_EFI:-}" ]; then
	mcopy -i "$image" "$driver" ::/DRIVER.EFI
	expected='Speech driver test passed'
fi

cp "$ovmf/OVMF_VARS_4M.fd" "$out/OVMF_VARS.fd"
: > "$out/serial.log"
qemu-system-x86_64 -machine q35 -m 256 -nic none -display none \
	-serial "file:$out/serial.log" -monitor none -qmp "unix:qmp.sock,server=on,wait=off" \
	-drive "if=pflash,format=raw,readonly=on,file=$ovmf/OVMF_CODE_4M.fd" \
	-drive "if=pflash,format=raw,file=$out/OVMF_VARS.fd" \
	-drive "if=virtio,format=raw,file=$image" > "$out/qemu.log" 2>&1 &
qemu=$!
trap 'kill "$qemu" 2>/dev/null || :; wait "$qemu" || :' EXIT HUP INT TERM
attempt=0
while ! grep -Fq "$expected" "$out/serial.log" 2>/dev/null; do
	if ! kill -0 "$qemu" 2>/dev/null || [ "$attempt" -ge 60 ]; then
		cat "$out/qemu.log"
		test ! -f "$out/serial.log" || cat "$out/serial.log"
		exit 1
	fi
	attempt=$((attempt + 1))
	sleep 0.25
done

python3 - qmp.sock <<'PY'
import json
import socket
import sys

with socket.socket(socket.AF_UNIX) as conn:
    conn.connect(sys.argv[1])
    with conn.makefile("rwb") as peer:
        assert "QMP" in json.loads(peer.readline())
        for command in ("qmp_capabilities", "quit"):
            peer.write(json.dumps({"execute": command}).encode() + b"\r\n")
            peer.flush()
            while True:
                reply = json.loads(peer.readline())
                if "return" in reply:
                    break
                assert "error" not in reply, reply
PY
wait "$qemu"
trap - EXIT HUP INT TERM
if [ -n "${DRIVER_EFI:-}" ]; then
	printf '%s\n' "$expected"
	exit 0
fi

mcopy -o -i "$image" ::/FIXED.WAV ::/DYNAMIC.WAV "$out/"
python3 - "$out" <<'PY'
from array import array
from hashlib import sha256
from pathlib import Path
import sys
import wave

out = Path(sys.argv[1])
for name in ("FIXED.WAV", "DYNAMIC.WAV"):
    with wave.open(str(out / name)) as pcm:
        frames = pcm.getnframes()
        assert (pcm.getframerate(), pcm.getnchannels(), pcm.getsampwidth()) == (48000, 2, 2)
        assert frames >= 4800
        data = pcm.readframes(frames)
    assert len(data) == frames * 4
    samples = array("h")
    samples.frombytes(data)
    if sys.byteorder != "little":
        samples.byteswap()
    assert samples[::2] == samples[1::2]
    peak = max(map(abs, samples))
    assert peak > 64
    digest = sha256(data).hexdigest()
    print(f"{name}: {frames} frames, peak {peak}, SHA-256 {digest}")
PY

if [ "$#" -lt 3 ]; then
	"$script" "$app" "$out/alternate" \
		'Power on. Open the café USB recovery entry. Press F twelve.'
	cmp "$out/FIXED.WAV" "$out/alternate/FIXED.WAV"
	if cmp -s "$out/DYNAMIC.WAV" "$out/alternate/DYNAMIC.WAV"; then
		printf '%s\n' 'Synthesis did not distinguish the runtime inputs' >&2
		exit 1
	fi
fi
