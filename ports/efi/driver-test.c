/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "port.h"

#define EDIIE_ABI_UEFI_H_
#define MS_ABI EFIAPI
#include <ediie/speech.h>

static EFI_GUID speech_guid = SPEECH_GUID;
static EFI_GUID loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID filesystem_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;

static UINTN sample_frames;
static bool heard_signal;

static EFI_STATUS EFIAPI record(void *context, const INT16 *samples, UINTN frames)
{
	(void)context;
	if (!frames || frames > 1024)
		return EFI_DEVICE_ERROR;

	for (UINTN frame = 0; frame < frames; frame++) {
		if (samples[frame * 2] != samples[frame * 2 + 1])
			return EFI_DEVICE_ERROR;
		if (samples[frame * 2])
			heard_signal = true;
	}
	sample_frames += frames;
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI record_warning(void *context, const INT16 *samples, UINTN frames)
{
	EFI_STATUS status = record(context, samples, frames);
	return status == EFI_SUCCESS ? EFI_WARN_UNKNOWN_GLYPH : status;
}

static EFI_STATUS EFIAPI record_to_limit(void *context, const INT16 *samples, UINTN frames)
{
	(void)samples;
	UINTN *total = context;
	*total += frames;
	return *total == (UINTN)PCM_RATE * 30 ? EFI_BUFFER_TOO_SMALL : EFI_SUCCESS;
}

static EFI_STATUS EFIAPI interrupt(void *context, const INT16 *samples, UINTN frames)
{
	(void)samples;
	(void)frames;
	struct speech_protocol *speech = context;
	if (speech->synthesize(speech, "Busy", record, NULL) != EFI_ACCESS_DENIED)
		return EFI_DEVICE_ERROR;
	return EFI_ABORTED;
}

static EFI_STATUS load_driver(EFI_HANDLE app, EFI_HANDLE *driver)
{
	EFI_LOADED_IMAGE_PROTOCOL *loaded;
	EFI_STATUS status = boot_services->HandleProtocol(app, &loaded_image_guid,
		(void **)&loaded);
	if (status != EFI_SUCCESS)
		return status;

	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
	status = boot_services->HandleProtocol(loaded->DeviceHandle,
		&filesystem_guid, (void **)&fs);
	if (status != EFI_SUCCESS)
		return status;

	EFI_FILE_PROTOCOL *root;
	status = fs->OpenVolume(fs, &root);
	if (status != EFI_SUCCESS)
		return status;

	EFI_FILE_PROTOCOL *file;
	status = root->Open(root, &file, L"DRIVER.EFI", EFI_FILE_MODE_READ, 0);
	root->Close(root);
	if (status != EFI_SUCCESS)
		return status;

	UINT64 file_size = 0;
	void *buffer = NULL;
	status = file->SetPosition(file, ~(UINT64)0);
	if (status == EFI_SUCCESS)
		status = file->GetPosition(file, &file_size);
	if (status == EFI_SUCCESS)
		status = file->SetPosition(file, 0);
	if (status == EFI_SUCCESS)
		status = boot_services->AllocatePool(EfiLoaderData, file_size, &buffer);
	UINTN read_size = file_size;
	if (status == EFI_SUCCESS)
		status = file->Read(file, &read_size, buffer);
	file->Close(file);
	if (status == EFI_SUCCESS && read_size != file_size)
		status = EFI_LOAD_ERROR;
	if (status == EFI_SUCCESS)
		status = boot_services->LoadImage(FALSE, app, NULL, buffer, file_size, driver);
	if (buffer)
		boot_services->FreePool(buffer);
	return status;
}

EFI_STATUS EFIAPI speech_driver_test_entry(EFI_HANDLE app, EFI_SYSTEM_TABLE *table)
{
	system_table = table;
	boot_services = table->BootServices;

	EFI_HANDLE driver;
	EFI_STATUS status = load_driver(app, &driver);
	if (status != EFI_SUCCESS) {
		console_write("Driver load failed\n");
		return status;
	}
	status = boot_services->StartImage(driver, NULL, NULL);
	if (status != EFI_SUCCESS) {
		console_write("Driver start failed\n");
		return status;
	}

	struct speech_protocol *speech;
	status = boot_services->LocateProtocol(&speech_guid, NULL, (void **)&speech);
	if (status != EFI_SUCCESS)
		goto unload;
	if (speech->synthesize(speech, "", record, NULL) != EFI_INVALID_PARAMETER) {
		status = EFI_DEVICE_ERROR;
		goto unload;
	}

	if (speech->synthesize(speech, "\001" "Vgmw/en", record, NULL) != EFI_INVALID_PARAMETER ||
	    speech->synthesize(speech, "\xc0\x81" "Vgmw/en", record, NULL) != EFI_INVALID_PARAMETER) {
		status = EFI_DEVICE_ERROR;
		goto unload;
	}

	char too_long[SPEECH_MAX_TEXT_BYTES + 2];
	for (UINTN i = 0; i < sizeof(too_long) - 1; i++)
		too_long[i] = 'x';
	too_long[sizeof(too_long) - 1] = 0;
	if (speech->synthesize(speech, too_long, record, NULL) != EFI_BAD_BUFFER_SIZE) {
		status = EFI_DEVICE_ERROR;
		goto unload;
	}

	const char text[] = "Boot option 42 is ready. The firmware version is 3.7.";
	status = speech->synthesize(speech, text, interrupt, speech);
	if (status != EFI_ABORTED) {
		status = EFI_DEVICE_ERROR;
		goto unload;
	}

	const char repeat[] = "Firmware boot options and diagnostics. ";
	for (UINTN i = 0; i < SPEECH_MAX_TEXT_BYTES; i++)
		too_long[i] = repeat[i % (sizeof(repeat) - 1)];
	too_long[SPEECH_MAX_TEXT_BYTES] = 0;

	UINTN total = 0;
	status = speech->synthesize(speech, too_long, record_to_limit, &total);
	if (status != EFI_BUFFER_TOO_SMALL || total != (UINTN)PCM_RATE * 30) {
		status = EFI_DEVICE_ERROR;
		goto unload;
	}

	status = speech->synthesize(speech, text, record_warning, NULL);
	if (status != EFI_WARN_UNKNOWN_GLYPH || sample_frames < 4800 || !heard_signal) {
		status = EFI_DEVICE_ERROR;
		goto unload;
	}
	status = EFI_SUCCESS;

unload:
	if (boot_services->UnloadImage(driver) != EFI_SUCCESS)
		return EFI_DEVICE_ERROR;
	if (status != EFI_SUCCESS)
		return status;
	if (boot_services->LocateProtocol(&speech_guid, NULL, (void **)&speech) != EFI_NOT_FOUND)
		return EFI_DEVICE_ERROR;

	console_write("Speech driver test passed\n");
	return EFI_SUCCESS;
}
