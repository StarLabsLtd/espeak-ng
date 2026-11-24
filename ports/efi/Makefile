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
PE_NAME ?= Speech.efi
COMPRESS ?= 1

engine_names := common mnemonics error ieee80 dictionary encoding intonation langopts \
	numbers phoneme phonemelist readclause setlengths soundicon spect ssml synthdata \
	synthesize tr_languages translate translateword voices wavegen speech
ucd_names := case categories ctype proplist scripts tostring
efi_names := $(engine_names) compiledata compiledict espeak_api
host_names := $(efi_names)
includes := -I$(PORT) -I$(ESPEAK)/src/include -I$(ESPEAK)/src/ucd-tools/src/include
efi_includes := $(includes) -I$(EFI) -I$(EFI)/x86_64 -I$(NANOPRINTF) -I$(LZMA)
efi_flags := -std=gnu11 -Os -D_GNU_SOURCE -D__NO_CTYPE=1 -D_GNU_EFI -DGNU_EFI_USE_MS_ABI \
	-ffreestanding -fno-builtin -fno-stack-protector -fpie -mcmodel=small \
	-fvisibility=hidden -fno-asynchronous-unwind-tables -fno-unwind-tables \
	-mno-red-zone -fno-ident -fcf-protection=none -U_FORTIFY_SOURCE \
	-D_FORTIFY_SOURCE=0 -fdata-sections -ffunction-sections -fwrapv
efi_objects := $(addprefix $(BUILD)/engine/,$(addsuffix .o,$(efi_names))) \
	$(addprefix $(BUILD)/ucd/,$(addsuffix .o,$(ucd_names))) \
	$(addprefix $(BUILD)/,main.o runtime.o voice_data.o archive.o)
ifeq ($(COMPRESS),1)
efi_objects += $(BUILD)/LzmaDec.o
endif
voice_names := en_dict phondata phonindex phontab intonations lang/gmw/en lang/gmw/en-US
voice_files := $(addprefix $(VOICE_DATA)/,$(voice_names))

.PHONY: all objects
all: $(BUILD)/$(PE_NAME)
objects: $(filter-out $(BUILD)/archive.o $(BUILD)/main.o $(BUILD)/runtime.o $(BUILD)/math.o $(BUILD)/voice_data.o,$(efi_objects))

$(BUILD)/engine/%.o: $(ESPEAK)/src/libespeak-ng/%.c $(PORT)/config.h $(PORT)/Makefile
	@mkdir -p "$(@D)"
	$(CC) $(efi_includes) $(efi_flags) -MMD -MP -c "$<" -o "$@"

$(BUILD)/ucd/%.o: $(ESPEAK)/src/ucd-tools/src/%.c $(PORT)/config.h $(PORT)/Makefile
	@mkdir -p "$(@D)"
	$(CC) $(efi_includes) $(efi_flags) -MMD -MP -c "$<" -o "$@"

$(BUILD)/host/espeak-ng: $(ESPEAK)/src/espeak-ng.c $(PORT)/config.h $(PORT)/Makefile \
	$(addprefix $(ESPEAK)/src/libespeak-ng/,$(addsuffix .c,$(host_names))) \
	$(addprefix $(ESPEAK)/src/ucd-tools/src/,$(addsuffix .c,$(ucd_names)))
	@mkdir -p "$(@D)"
	$(HOSTCC) $(includes) -I$(ESPEAK)/src/libespeak-ng -D_GNU_SOURCE -O2 -fwrapv \
		"$<" $(filter %.c,$(wordlist 2,999,$^)) -lm -o "$@"

$(BUILD)/data/en_dict $(BUILD)/data/phondata $(BUILD)/data/phonindex \
$(BUILD)/data/phontab $(BUILD)/data/intonations $(BUILD)/data/lang/gmw/en \
$(BUILD)/data/lang/gmw/en-US &: $(BUILD)/host/espeak-ng
	mkdir -p "$(BUILD)/data/lang/gmw"
	ln -sfn "$(ESPEAK)/phsource" "$(BUILD)/phsource"
	cp "$(ESPEAK)/espeak-ng-data/lang/gmw/en" "$(ESPEAK)/espeak-ng-data/lang/gmw/en-US" \
		"$(BUILD)/data/lang/gmw/"
	cd "$(ESPEAK)" && ESPEAK_DATA_PATH="$(BUILD)/data" "$(BUILD)/host/espeak-ng" --compile-intonations
	cd "$(ESPEAK)" && ESPEAK_DATA_PATH="$(BUILD)/data" "$(BUILD)/host/espeak-ng" --compile-phonemes
	cd "$(ESPEAK)/dictsource" && ESPEAK_DATA_PATH="$(BUILD)/data" "$(BUILD)/host/espeak-ng" --compile=en

$(BUILD)/archive.S $(BUILD)/voice_files.h &: $(PORT)/prepare-data.py $(voice_files)
	python3 "$<" "$(BUILD)" "$(VOICE_DATA)" "$(COMPRESS)" $(voice_names)

$(BUILD)/archive.o: $(BUILD)/archive.S
	$(CC) $(efi_flags) -c "$<" -o "$@"

$(BUILD)/voice_data.o: $(BUILD)/voice_files.h
$(BUILD)/main.o: efi_flags += -fshort-wchar
$(BUILD)/%.o: $(PORT)/%.c $(PORT)/Makefile
	@mkdir -p "$(@D)"
	$(CC) $(efi_includes) $(efi_flags) -I$(BUILD) -DCOMPRESS_VOICE=$(COMPRESS) \
		-Wall -Wextra -Werror -MMD -MP -c "$<" -o "$@"

$(BUILD)/LzmaDec.o: $(LZMA)/LzmaDec.c
	@mkdir -p "$(@D)"
	$(CC) $(efi_includes) $(efi_flags) -c "$<" -o "$@"

$(BUILD)/native-pe-link: $(EDIIE)/util/native_pe_link.c
	@mkdir -p "$(@D)"
	$(HOSTCC) -std=c11 -O2 -Wall -Wextra -Werror "$<" -o "$@"

$(BUILD)/pe-exec-sections: $(EDIIE)/util/pe_exec_sections.c
	@mkdir -p "$(@D)"
	$(HOSTCC) -std=c11 -O2 -Wall -Wextra -Werror "$<" -o "$@"

$(BUILD)/pe_relocation_marker.o: $(EDIIE)/src/lib/pe_relocation_marker.c
	@mkdir -p "$(@D)"
	$(CC) $(efi_flags) -I$(EDIIE)/include -c "$<" -o "$@"

$(BUILD)/speech-thin.o: $(efi_objects)
	$(LD) -r --gc-sections -u efi_main -o "$@" $^

$(BUILD)/speech.o: $(BUILD)/speech-thin.o $(MUSL)
	$(LD) -r --gc-sections -u efi_main -o "$@" $^

$(BUILD)/$(PE_NAME): $(BUILD)/speech.o $(BUILD)/native-pe-link $(BUILD)/pe-exec-sections \
	$(BUILD)/pe_relocation_marker.o $(EDIIE)/src/boot/native_module.ld
	"$(BUILD)/native-pe-link" "$(LD)" "$(BUILD)/pe-exec-sections" \
		"$(BUILD)/pe_relocation_marker.o" "$(READELF)" \
		--subsystem 10 --entry efi_main --image-base 0 \
		--section-alignment 0x1000 --file-alignment 0x200 --build-id=none \
		--no-insert-timestamp -s --nxcompat -T "$(EDIIE)/src/boot/native_module.ld" \
		-o "$@" "$<"

READELF ?= readelf
-include $(efi_objects:.o=.d)
