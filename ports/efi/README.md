# Standalone English speech on x86-64 EFI

This local prototype links eSpeak NG 1.52.0 into an EFI application. The
application reads UTF-8 text from `TEXT.TXT` at the root of its boot volume,
then writes the fixed and runtime utterances as `FIXED.WAV` and
`DYNAMIC.WAV`. It replaces existing files with those names. Both WAVs use
48,000 Hz, signed 16-bit interleaved stereo PCM. Both channels carry the
same signal. Resampling uses linear interpolation across engine callbacks.

The application uses EFI boot services for memory and a read-only virtual
filesystem for the seven generated eSpeak data files. The compressed build
expands the archive when the application is launched and frees it before
returning. The engine copies voice and dictionary inputs into its own state
during initialisation. The generated phoneme files include the upstream set
of cross-language tables. The bundled dictionary is English and the selected
voice is US English. The application writes WAV files, without depending on
a sound device or a firmware audio protocol.

## Build

Use GNU make 4.3 or later, GCC, binutils with the `i386pep` backend, and
Python 3 with `lzma`. The tested compiler was GCC 15.2.0 with glibc headers.
This directory belongs in the eSpeak NG source at
`4870adfa25b1a32b4361592f1be8a40337c58d6c` (tag `1.52.0`).

Prepare these dependencies beside the eSpeak checkout, or pass their paths to
make. None is fetched by the build.

| Make argument | Input |
| --- | --- |
| `EDIIE` | edIIe checkout at `c2647eb8223ce4d583e58a1123f877ae4b20f767` for its PE linker, checker and linker script |
| `EFI` | GNU-EFI 3.0.18 headers. Tested with Ubuntu package `gnu-efi_3.0.18-1_amd64.deb` extracted under `../efi` |
| `NANOPRINTF` | nanoprintf checkout at `bebaecbeaf430e36a2d30d31a2fed4e098388c93` |
| `LZMA` | coreboot's `util/cbfstool/lzma/C` at `660411d22bc98299ef07bbf38cfdbcfc9644a141` |
| `MUSL` | Source-built musl 1.2.5 `libc.a`, prepared as shown below |

The musl 1.2.5 archive from `https://musl.libc.org/releases/` has SHA-256
`a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4`.
The Ubuntu GNU-EFI package used here has SHA-256
`28dbc5aae0a735ffcce160705a183db7fafc71516ba98275e16ed6ceda1a6bd7`.

From the eSpeak NG checkout, with the musl source extracted beside it:

```sh
mkdir -p ../musl-build
(
	cd ../musl-build
	../musl-1.2.5/configure --disable-shared \
		CFLAGS='-Os -mno-red-zone -fno-stack-protector -fdata-sections -ffunction-sections'
	make -j8 lib/libc.a
)
make -f ports/efi/Makefile -j8 LZMA=/path/to/coreboot/util/cbfstool/lzma/C
```

The build compiles the eSpeak host tool to generate the English dictionary,
phoneme data and intonations. It links the firmware runtime with only the
needed musl math and string functions. Build musl with the flags above to
avoid pulling in host thread-local stack-protector state. The resulting
`../build/Speech.efi` has no PE imports.

For an uncompressed comparison, use a separate output directory and reuse
the generated data:

```sh
make -f ports/efi/Makefile -j8 \
	BUILD="$(pwd)/../build-raw" VOICE_DATA="$(pwd)/../build/data" COMPRESS=0 \
	LZMA=/path/to/coreboot/util/cbfstool/lzma/C
```

## OVMF test

The harness requires QEMU, OVMF, mtools, and Python 3. Give it an output
directory for the VM image and generated files.

```sh
ports/efi/test-ovmf.sh ../build/Speech.efi ../test-english
ports/efi/test-ovmf.sh ../build/Speech.efi ../test-second-input \
	'Power on. Open the café USB recovery entry. Press F twelve.'
cmp ../test-english/FIXED.WAV ../test-second-input/FIXED.WAV
```

The harness checks the EFI success result, WAV framing, non-silence, identical
left and right samples, and a distinct digest for each utterance. The fixed
utterance should match across both boots, while the runtime utterances
should differ.

## Size

The English inputs total 827,621 bytes before compression and 379,796
bytes as an LZMA archive. The compressed-archive PE is 790,528 bytes.
Copying the existing Lite board ROM and adding the PE as a standalone raw
CBFS file consumed 508,160 bytes with CBFS LZMA or 790,592 bytes without it.
These are free-space changes from `cbfstool print`, including alignment and
record overhead. The experiment did not embed the application in edIIe's
direct-image table.

## Licensing

The prototype sources and eSpeak NG retain GPL-3.0-or-later licensing.
See eSpeak NG's COPYING and COPYING.UCD for the engine and Unicode database
notices. The dependencies retain their own notices. GNU-EFI's package
documents the licenses covering its headers. See nanoprintf's LICENSE and
musl's COPYRIGHT for the code used in the binary. The LZMA decoder headers
and implementation carry public-domain notices. The standalone prototype
does not resolve the terms for distributing eSpeak NG with edIIe.
