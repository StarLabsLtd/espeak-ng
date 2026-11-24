/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "port.h"
#include <string.h>

/* Use the same public declaration with GNU-EFI's implementations of the ABI. */
#define EDIIE_ABI_UEFI_H_
#define MS_ABI EFIAPI
#include <ediie/speech.h>

static EFI_GUID speech_guid = SPEECH_GUID;
static EFI_GUID loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;

static EFI_STATUS EFIAPI synthesize(struct speech_protocol *protocol, const char *text,
	speech_pcm_sink_fn *sink, void *context)
{
	(void)protocol;
	if (!text)
		return EFI_INVALID_PARAMETER;
	size_t length = strnlen(text, SPEECH_MAX_TEXT_BYTES + 1);
	if (length > SPEECH_MAX_TEXT_BYTES)
		return EFI_BAD_BUFFER_SIZE;
	return speech_render(text, length, sink, context);
}

static struct speech_protocol speech = { synthesize };

static EFI_STATUS EFIAPI unload(EFI_HANDLE image)
{
	if (speech_busy())
		return EFI_ACCESS_DENIED;

	EFI_STATUS status = boot_services->UninstallProtocolInterface(image, &speech_guid, &speech);
	if (status != EFI_SUCCESS)
		return status;

	speech_release();
	return EFI_SUCCESS;
}

EFI_STATUS EFIAPI speech_driver_entry(EFI_HANDLE image, EFI_SYSTEM_TABLE *table)
{
	/* The graphical caller owns its console while speech is active. */
	boot_services = table->BootServices;

	EFI_LOADED_IMAGE_PROTOCOL *loaded_image;
	EFI_STATUS status = boot_services->HandleProtocol(image, &loaded_image_guid,
		(void **)&loaded_image);
	if (status != EFI_SUCCESS)
		return status;

	status = boot_services->InstallProtocolInterface(&image, &speech_guid,
		EFI_NATIVE_INTERFACE, &speech);
	if (status == EFI_SUCCESS)
		loaded_image->Unload = unload;
	return status;
}
