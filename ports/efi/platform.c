/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "port.h"

EFI_BOOT_SERVICES *boot_services;
EFI_SYSTEM_TABLE *system_table;

void console_write(const char *text)
{
	if (!system_table || !system_table->ConOut)
		return;

	CHAR16 buffer[128];
	while (*text) {
		size_t length = 0;
		while (*text && length < sizeof(buffer) / sizeof(*buffer) - 2) {
			if (*text == '\n')
				buffer[length++] = (CHAR16)'\r';
			buffer[length++] = (unsigned char)*text++;
		}
		buffer[length] = 0;
		system_table->ConOut->OutputString(system_table->ConOut, buffer);
	}
}
