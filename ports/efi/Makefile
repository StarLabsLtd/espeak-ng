# SPDX-License-Identifier: GPL-3.0-or-later
# Local standalone prototype. See README.md for the dependency pins.
ESPEAK := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
PORT := $(ESPEAK)/ports/efi
BUILD ?= $(ESPEAK)/../build
EDIIE ?= $(ESPEAK)/../edIIe
EFI ?= $(ESPEAK)/../efi/usr/include/efi
NANOPRINTF ?= $(ESPEAK)/../nanoprintf
LZMA ?= $(ESPEAK)/../coreboot/util/cbfstool/lzma/C
MUSL ?= $(ESPEAK)/../musl-build/lib/libc.a
VOICE_DATA ?= $(BUILD)/data
HOSTCC ?= cc
COMPRESS ?= 1

engine_names := common mnemonics error ieee80 dictionary encoding intonation langopts \
	numbers phoneme phonemelist readclause setlengths soundicon spect ssml synthdata \
	synthesize tr_languages translate translateword voices wavegen speech
ucd_names := case categories ctype proplist scripts tostring
efi_names := $(engine_names) compiledata compiledict espeak_api
host_headers := $(wildcard $(ESPEAK)/src/libespeak-ng/*.h \
	$(ESPEAK)/src/include/espeak-ng/*.h $(ESPEAK)/src/ucd-tools/src/include/ucd/*.h)
includes := -I$(PORT) -I$(ESPEAK)/src/include -I$(ESPEAK)/src/ucd-tools/src/include
efi_includes := $(includes) -I$(EFI) -I$(EFI)/x86_64 -I$(NANOPRINTF) -I$(LZMA)
efi_flags := -std=gnu11 -Os -D_GNU_SOURCE -D__NO_CTYPE=1 -D_GNU_EFI -DGNU_EFI_USE_MS_ABI \
	-ffreestanding -fno-builtin -fno-stack-protector -fpie -mcmodel=small \
	-fvisibility=hidden -fno-asynchronous-unwind-tables -fno-unwind-tables \
	-mno-red-zone -fno-ident -fcf-protection=none -U_FORTIFY_SOURCE \
	-D_FORTIFY_SOURCE=0 -fdata-sections -ffunction-sections -fwrapv \
	-ffile-prefix-map=$(ESPEAK)=.
efi_objects := $(addprefix $(BUILD)/engine/,$(addsuffix .o,$(efi_names))) \
	$(addprefix $(BUILD)/ucd/,$(addsuffix .o,$(ucd_names))) \
	$(addprefix $(BUILD)/,engine.o platform.o runtime.o voice_data.o archive.o)
ifeq ($(COMPRESS),1)
efi_objects += $(BUILD)/LzmaDec.o
endif
voice_names := en_dict phondata phonindex phontab intonations lang/gmw/en lang/gmw/en-US
voice_files := $(addprefix $(VOICE_DATA)/,$(voice_names))
voice_sources := $(wildcard $(ESPEAK)/dictsource/en_*) \
	$(ESPEAK)/espeak-ng-data/lang/gmw/en $(ESPEAK)/espeak-ng-data/lang/gmw/en-US \
	$(shell find "$(ESPEAK)/phsource" -type f ! -name compile_prog_log)

.DELETE_ON_ERROR:
.PHONY: all speech-driver driver-test
all: $(BUILD)/Speech.efi
speech-driver: $(BUILD)/SpeechDxe.efi
driver-test: $(BUILD)/SpeechDriverTest.efi $(BUILD)/SpeechDxe.efi

$(BUILD)/engine/%.o: $(ESPEAK)/src/libespeak-ng/%.c $(PORT)/config.h $(PORT)/Makefile
	@mkdir -p "$(@D)"
	$(CC) $(efi_includes) $(efi_flags) -MMD -MP -c "$<" -o "$@"

$(BUILD)/ucd/%.o: $(ESPEAK)/src/ucd-tools/src/%.c $(PORT)/config.h $(PORT)/Makefile
	@mkdir -p "$(@D)"
	$(CC) $(efi_includes) $(efi_flags) -MMD -MP -c "$<" -o "$@"

$(BUILD)/host/espeak-ng: $(ESPEAK)/src/espeak-ng.c $(PORT)/config.h $(PORT)/Makefile $(host_headers) \
	$(addprefix $(ESPEAK)/src/libespeak-ng/,$(addsuffix .c,$(efi_names))) \
	$(addprefix $(ESPEAK)/src/ucd-tools/src/,$(addsuffix .c,$(ucd_names)))
	@mkdir -p "$(@D)"
	$(HOSTCC) $(includes) -I$(ESPEAK)/src/libespeak-ng -D_GNU_SOURCE -O2 -fwrapv \
		"$<" $(filter %.c,$(wordlist 2,999,$^)) -lm -o "$@"

$(BUILD)/data/en_dict $(BUILD)/data/phondata $(BUILD)/data/phonindex \
$(BUILD)/data/phontab $(BUILD)/data/intonations $(BUILD)/data/lang/gmw/en \
$(BUILD)/data/lang/gmw/en-US &: $(BUILD)/host/espeak-ng $(voice_sources)
	@set -eu; \
	work=$$(mktemp -d "$(abspath $(BUILD))/voice-gen.XXXXXX"); \
	trap 'rm -rf "$$work"' EXIT; \
	mkdir -p "$$work/data/lang/gmw"; \
	cp -a "$(ESPEAK)/phsource" "$$work/"; \
	cp "$(ESPEAK)/espeak-ng-data/lang/gmw/en" "$(ESPEAK)/espeak-ng-data/lang/gmw/en-US" \
		"$$work/data/lang/gmw/"; \
	ESPEAK_DATA_PATH="$$work/data" "$(BUILD)/host/espeak-ng" --compile-intonations; \
	ESPEAK_DATA_PATH="$$work/data" "$(BUILD)/host/espeak-ng" --compile-phonemes; \
	(cd "$(ESPEAK)/dictsource" && ESPEAK_DATA_PATH="$$work/data" \
		"$(abspath $(BUILD))/host/espeak-ng" --compile=en); \
	mkdir -p "$(BUILD)/data"; \
	cp -a "$$work/data/." "$(BUILD)/data/"


$(BUILD)/archive.S $(BUILD)/voice_files.h $(BUILD)/voice-data.bin &: $(PORT)/prepare-data.py $(voice_files)
	@mkdir -p "$(@D)"
	python3 "$<" "$(BUILD)" "$(VOICE_DATA)" "$(COMPRESS)" $(voice_names)

$(BUILD)/archive.o: $(BUILD)/archive.S $(BUILD)/voice-data.bin
	$(CC) $(efi_flags) -c "$<" -o "$@"

$(BUILD)/voice_data.o: $(BUILD)/voice_files.h
$(BUILD)/main.o $(BUILD)/platform.o $(BUILD)/driver.o $(BUILD)/driver-test.o: efi_flags += -fshort-wchar
$(BUILD)/driver.o $(BUILD)/driver-test.o: efi_includes += -I$(EDIIE)/include
$(BUILD)/%.o: $(PORT)/%.c $(PORT)/Makefile
	@mkdir -p "$(@D)"
	$(CC) $(efi_includes) $(efi_flags) -I$(BUILD) -DCOMPRESS_VOICE=$(COMPRESS) \
		-Wall -Wextra -Werror -MMD -MP -c "$<" -o "$@"

$(BUILD)/LzmaDec.o: $(LZMA)/LzmaDec.c $(PORT)/Makefile
	@mkdir -p "$(@D)"
	$(CC) $(efi_includes) $(efi_flags) -MMD -MP -c "$<" -o "$@"

$(BUILD)/native-pe-link: $(EDIIE)/util/native_pe_link.c
	@mkdir -p "$(@D)"
	$(HOSTCC) -std=c11 -O2 -Wall -Wextra -Werror "$<" -o "$@"

$(BUILD)/pe-exec-sections: $(EDIIE)/util/pe_exec_sections.c
	@mkdir -p "$(@D)"
	$(HOSTCC) -std=c11 -O2 -Wall -Wextra -Werror "$<" -o "$@"

$(BUILD)/pe_relocation_marker.o: $(EDIIE)/src/lib/pe_relocation_marker.c $(PORT)/Makefile
	@mkdir -p "$(@D)"
	$(CC) $(efi_flags) -I$(EDIIE)/include -MMD -MP -c "$<" -o "$@"

$(BUILD)/speech-thin.o: $(BUILD)/main.o $(efi_objects)
	$(LD) -r --gc-sections -u efi_main -o "$@" $^

$(BUILD)/speech.o: $(BUILD)/speech-thin.o $(MUSL)
	$(LD) -r --gc-sections -u efi_main -o "$@" $^

$(BUILD)/speech-driver-thin.o: $(BUILD)/driver.o $(efi_objects)
	$(LD) -r --gc-sections -u speech_driver_entry -o "$@" $^

$(BUILD)/speech-driver.o: $(BUILD)/speech-driver-thin.o $(MUSL)
	$(LD) -r --gc-sections -u speech_driver_entry -o "$@" $^

$(BUILD)/speech-driver-test.o: $(BUILD)/driver-test.o $(BUILD)/platform.o
	$(LD) -r --gc-sections -u speech_driver_test_entry -o "$@" $^

$(BUILD)/Speech.efi: entry := efi_main
$(BUILD)/Speech.efi: subsystem := 10
$(BUILD)/Speech.efi: input := $(BUILD)/speech.o
$(BUILD)/Speech.efi: $(BUILD)/speech.o
$(BUILD)/SpeechDxe.efi: entry := speech_driver_entry
$(BUILD)/SpeechDxe.efi: subsystem := 11
$(BUILD)/SpeechDxe.efi: input := $(BUILD)/speech-driver.o
$(BUILD)/SpeechDxe.efi: $(BUILD)/speech-driver.o

$(BUILD)/SpeechDriverTest.efi: entry := speech_driver_test_entry
$(BUILD)/SpeechDriverTest.efi: subsystem := 10
$(BUILD)/SpeechDriverTest.efi: input := $(BUILD)/speech-driver-test.o
$(BUILD)/SpeechDriverTest.efi: $(BUILD)/speech-driver-test.o

$(BUILD)/Speech.efi $(BUILD)/SpeechDxe.efi $(BUILD)/SpeechDriverTest.efi: $(BUILD)/native-pe-link $(BUILD)/pe-exec-sections \
	$(BUILD)/pe_relocation_marker.o $(EDIIE)/src/boot/native_module.ld
	"$(BUILD)/native-pe-link" "$(LD)" "$(BUILD)/pe-exec-sections" \
		"$(BUILD)/pe_relocation_marker.o" "$(READELF)" \
		--subsystem $(subsystem) --entry $(entry) --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x200 --build-id=none \
		--no-insert-timestamp -s --nxcompat -T "$(EDIIE)/src/boot/native_module.ld" \
		-o "$@" "$(input)"

READELF ?= readelf
-include $(efi_objects:.o=.d) $(BUILD)/main.d $(BUILD)/driver.d $(BUILD)/driver-test.d \
	$(BUILD)/pe_relocation_marker.d
