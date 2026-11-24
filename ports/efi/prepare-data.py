#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import lzma
from pathlib import Path
import struct
import sys

build, source = map(Path, sys.argv[1:3])
compressed = sys.argv[3] == "1"
names = sys.argv[4:]
data = bytearray()
rows = []
for name in names:
    contents = (source / name).read_bytes()
    rows.append(f'\t{{ "/voice/{name}", {len(data)}, {len(contents)} }},')
    data.extend(contents)

archive = build / "voice-data.bin"
encoded = lzma.compress(data, format=lzma.FORMAT_ALONE) if compressed else data
if compressed:
    # The SDK decoder receives the exact destination size from the generated header.
    assert encoded[5:13] == b"\xff" * 8
    encoded = encoded[:5] + struct.pack("<Q", len(data)) + encoded[13:]
archive.write_bytes(encoded)
(build / "voice_files.h").write_text(
    f"#define VOICE_DATA_BYTES {len(data)}\n"
    "static const struct voice_file voice_files[] = {\n"
    + "\n".join(rows)
    + "\n};\n"
)
(build / "archive.S").write_text(
    '.section .rodata.voice,"a",@progbits\n'
    ".balign 8\n"
    ".globl voice_archive_start, voice_archive_end\n"
    ".hidden voice_archive_start, voice_archive_end\n"
    "voice_archive_start:\n"
    f'.incbin "{archive.resolve()}"\n'
    "voice_archive_end:\n"
    '.section .note.GNU-stack,"",@progbits\n'
)
