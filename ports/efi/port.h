/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ESPEAK_EFI_PORT_H
#define ESPEAK_EFI_PORT_H
#include <efi.h>
#include <stdbool.h>
#include <stddef.h>

#define PCM_RATE 48000U
#define PCM_CHANNELS 2U

typedef EFI_STATUS EFIAPI pcm_sink_fn(void *context, const INT16 *samples, UINTN frames);

extern EFI_BOOT_SERVICES *boot_services;
extern EFI_SYSTEM_TABLE *system_table;

EFI_STATUS load_voice_data(void);
void release_voice_data(void);
const unsigned char *voice_file_data(const char *path, size_t *size);
void console_write(const char *str);
void release_speech_allocations(void);

EFI_STATUS speech_render(const char *text, size_t length, pcm_sink_fn *sink, void *context);
bool speech_busy(void);
void speech_release(void);

#endif
