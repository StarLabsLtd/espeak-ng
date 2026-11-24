/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ESPEAK_EFI_PORT_H
#define ESPEAK_EFI_PORT_H
#include <efi.h>
#include <stdbool.h>
#include <stddef.h>

extern EFI_BOOT_SERVICES *boot_services;
extern EFI_SYSTEM_TABLE *system_table;

bool load_voice_data(void);
void release_voice_data(void);
const unsigned char *voice_file_data(const char *path, size_t *size);
void console_write(const char *str);

#endif
