/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "port.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PCM_FRAME_BYTES (sizeof(int16_t) * PCM_CHANNELS)

static EFI_GUID loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID file_system_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;

static EFI_HANDLE app_handle;
static EFI_FILE_PROTOCOL *output;
static uint32_t written_frames;

static EFI_STATUS write_file(EFI_FILE_PROTOCOL *file, const void *data, UINTN length)
{
	UINTN written = length;
	EFI_STATUS status = file->Write(file, &written, (void *)data);
	if (!EFI_ERROR(status) && written != length)
		return EFI_VOLUME_FULL;
	return status;
}

static EFI_STATUS EFIAPI write_frames(void *context, const INT16 *samples, UINTN frames)
{
	EFI_STATUS status = write_file(context, samples, frames * PCM_FRAME_BYTES);
	if (!EFI_ERROR(status))
		written_frames += frames;
	return status;
}

struct __attribute__((packed)) wav_header {
	char riff[4];
	uint32_t riff_bytes;
	char wave[4];
	char format[4];
	uint32_t format_bytes;
	uint16_t format_type;
	uint16_t channels;
	uint32_t sample_rate;
	uint32_t bytes_per_second;
	uint16_t block_bytes;
	uint16_t bits_per_sample;
	char data[4];
	uint32_t data_bytes;
};

_Static_assert(sizeof(struct wav_header) == 44, "PCM WAV header size");

static EFI_STATUS write_header(void)
{
	uint32_t bytes = written_frames * PCM_FRAME_BYTES;
	const struct wav_header header = {
		.riff = { 'R', 'I', 'F', 'F' }, .riff_bytes = sizeof(header) - 8 + bytes, .wave = { 'W', 'A', 'V', 'E' },
		.format = { 'f', 'm', 't', ' ' }, .format_bytes = 16, .format_type = 1,
		.channels = PCM_CHANNELS, .sample_rate = PCM_RATE,
		.bytes_per_second = PCM_RATE * PCM_FRAME_BYTES,
		.block_bytes = PCM_FRAME_BYTES, .bits_per_sample = 16,
		.data = { 'd', 'a', 't', 'a' }, .data_bytes = bytes,
	};
	EFI_STATUS status = output->SetPosition(output, 0);
	if (EFI_ERROR(status))
		return status;
	return write_file(output, &header, sizeof(header));
}

static EFI_STATUS synthesize(EFI_FILE_PROTOCOL *root, CHAR16 *name, const char *text)
{
	written_frames = 0;
	EFI_STATUS status = root->Open(root, &output, name,
		EFI_FILE_MODE_WRITE | EFI_FILE_MODE_READ, 0);
	if (status == EFI_SUCCESS) {
		status = output->Delete(output);
		if (status == EFI_WARN_DELETE_FAILURE)
			status = EFI_DEVICE_ERROR;
	} else if (status == EFI_NOT_FOUND)
		status = EFI_SUCCESS;
	if (status != EFI_SUCCESS)
		return status;

	status = root->Open(root, &output, name,
		EFI_FILE_MODE_CREATE | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_READ, 0);
	if (EFI_ERROR(status))
		return status;

	status = write_header();
	if (!EFI_ERROR(status))
		status = speech_render(text, strlen(text), write_frames, output);
	if (!EFI_ERROR(status))
		status = write_header();

	if (EFI_ERROR(status)) {
		output->Delete(output);
		return status;
	}

	status = output->Close(output);
	if (!EFI_ERROR(status))
		printf("Wrote %u PCM frames at %u Hz, %u channels\n",
			written_frames, PCM_RATE, PCM_CHANNELS);
	return status;
}

static EFI_STATUS open_volume(EFI_FILE_PROTOCOL **root)
{
	EFI_LOADED_IMAGE_PROTOCOL *image;
	EFI_STATUS status = boot_services->HandleProtocol(app_handle,
		&loaded_image_guid, (void **)&image);
	if (EFI_ERROR(status))
		return status;

	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
	status = boot_services->HandleProtocol(image->DeviceHandle,
		&file_system_guid, (void **)&fs);
	if (EFI_ERROR(status))
		return status;
	return fs->OpenVolume(fs, root);
}

static EFI_STATUS read_input(EFI_FILE_PROTOCOL *root, char *text, size_t capacity)
{
	EFI_FILE_PROTOCOL *file;
	EFI_STATUS status = root->Open(root, &file, L"TEXT.TXT", EFI_FILE_MODE_READ, 0);
	if (EFI_ERROR(status))
		return status;

	UINTN size = capacity;
	status = file->Read(file, &size, text);
	EFI_STATUS closed = file->Close(file);
	if (EFI_ERROR(status))
		return status;
	if (EFI_ERROR(closed))
		return closed;
	if (size == capacity)
		return EFI_BAD_BUFFER_SIZE;
	if (!size || memchr(text, 0, size))
		return EFI_INVALID_PARAMETER;
	text[size] = 0;
	return EFI_SUCCESS;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *table)
{
	app_handle = image;
	system_table = table;
	boot_services = table->BootServices;

	EFI_FILE_PROTOCOL *root;
	EFI_STATUS status = open_volume(&root);
	if (EFI_ERROR(status))
		return status;

	static char input[1025];
	status = read_input(root, input, sizeof(input));
	if (EFI_ERROR(status)) {
		printf("TEXT.TXT read failed (%lx)\n", (unsigned long)status);
		goto close_volume;
	}

	status = synthesize(root, L"FIXED.WAV",
		"Firmware settings. Use the arrow keys to select a boot device.");
	if (!EFI_ERROR(status))
		status = synthesize(root, L"DYNAMIC.WAV", input);

	speech_release();
close_volume:
	root->Close(root);
	printf("Speech prototype returned %lx\n", (unsigned long)status);
	return status;
}
