/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "port.h"

#include <espeak-ng/espeak_ng.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PCM_RATE 48000U
#define PCM_CHANNELS 2U
#define PCM_FRAME_BYTES (sizeof(int16_t) * PCM_CHANNELS)

EFI_BOOT_SERVICES *boot_services;
EFI_SYSTEM_TABLE *system_table;

static EFI_GUID loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID file_system_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;

static EFI_HANDLE app_handle;
static unsigned int source_rate;

static struct {
	EFI_FILE_PROTOCOL *file;
	EFI_STATUS error;
	uint32_t frames;
	uint32_t phase;
	size_t buffer_frames;
	int16_t buffer[1024][PCM_CHANNELS];
	int16_t previous;
	bool have_sample;
} output;

void console_write(const char *text)
{
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

static EFI_STATUS write_file(EFI_FILE_PROTOCOL *file, const void *data, UINTN length)
{
	UINTN written = length;
	EFI_STATUS status = file->Write(file, &written, (void *)data);
	if (!EFI_ERROR(status) && written != length)
		return EFI_VOLUME_FULL;
	return status;
}

static EFI_STATUS flush_output(void)
{
	if (EFI_ERROR(output.error) || !output.buffer_frames)
		return output.error;

	output.error = write_file(output.file, output.buffer,
		output.buffer_frames * PCM_FRAME_BYTES);
	output.buffer_frames = 0;
	return output.error;
}

static void output_sample(int16_t sample)
{
	if (!output.have_sample) {
		output.have_sample = true;
		output.previous = sample;
		return;
	}

	while (output.phase < PCM_RATE) {
		int32_t distance = (int32_t)sample - output.previous;
		int16_t value = output.previous +
			(int64_t)distance * output.phase / PCM_RATE;
		output.buffer[output.buffer_frames][0] = value;
		output.buffer[output.buffer_frames][1] = value;
		output.buffer_frames++;
		output.phase += source_rate;
		output.frames++;
		if (output.buffer_frames == sizeof(output.buffer) / sizeof(output.buffer[0]) &&
		    EFI_ERROR(flush_output()))
			return;
	}

	output.previous = sample;
	output.phase -= PCM_RATE;
}

static int receive_samples(short *mono, int frames, espeak_EVENT *events)
{
	(void)events;
	for (int frame = 0; frame < frames; frame++) {
		output_sample(mono[frame]);
		if (EFI_ERROR(output.error))
			return 1;
	}
	return 0;
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
	uint32_t bytes = output.frames * PCM_FRAME_BYTES;
	const struct wav_header header = {
		.riff = { 'R', 'I', 'F', 'F' }, .riff_bytes = sizeof(header) - 8 + bytes, .wave = { 'W', 'A', 'V', 'E' },
		.format = { 'f', 'm', 't', ' ' }, .format_bytes = 16, .format_type = 1,
		.channels = PCM_CHANNELS, .sample_rate = PCM_RATE,
		.bytes_per_second = PCM_RATE * PCM_FRAME_BYTES,
		.block_bytes = PCM_FRAME_BYTES, .bits_per_sample = 16,
		.data = { 'd', 'a', 't', 'a' }, .data_bytes = bytes,
	};
	EFI_STATUS status = output.file->SetPosition(output.file, 0);
	if (EFI_ERROR(status))
		return status;
	return write_file(output.file, &header, sizeof(header));
}

static EFI_STATUS synthesize(EFI_FILE_PROTOCOL *root, CHAR16 *name, const char *text)
{
	memset(&output, 0, sizeof(output));
	EFI_STATUS status = root->Open(root, &output.file, name,
		EFI_FILE_MODE_WRITE | EFI_FILE_MODE_READ, 0);
	if (status == EFI_SUCCESS)
		status = output.file->Delete(output.file);
	else if (status == EFI_NOT_FOUND)
		status = EFI_SUCCESS;
	if (status != EFI_SUCCESS)
		return status;

	status = root->Open(root, &output.file, name,
		EFI_FILE_MODE_CREATE | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_READ, 0);
	if (EFI_ERROR(status))
		return status;

	status = write_header();
	if (!EFI_ERROR(status)) {
		espeak_ng_STATUS result = espeak_ng_Synthesize(text, strlen(text) + 1, 0,
			POS_CHARACTER, 0, espeakCHARS_UTF8, NULL, NULL);
		status = output.error;
		if (!EFI_ERROR(status) && result != ENS_OK) {
			printf("eSpeak synthesis failed (%x)\n", result);
			status = EFI_DEVICE_ERROR;
		}
	}

	if (!EFI_ERROR(status) && output.have_sample) {
		output_sample(output.previous);
		status = flush_output();
	}
	if (!EFI_ERROR(status) && !output.frames)
		status = EFI_NOT_FOUND;
	if (!EFI_ERROR(status))
		status = write_header();

	if (EFI_ERROR(status)) {
		output.file->Delete(output.file);
		return status;
	}

	status = output.file->Close(output.file);
	if (!EFI_ERROR(status))
		printf("Wrote %u PCM frames at %u Hz, %u channels\n",
			output.frames, PCM_RATE, PCM_CHANNELS);
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

	static char input[4096];
	status = read_input(root, input, sizeof(input));
	if (EFI_ERROR(status)) {
		printf("TEXT.TXT read failed (%lx)\n", (unsigned long)status);
		goto close_volume;
	}

	if (!load_voice_data()) {
		console_write("English archive decompression failed\n");
		status = EFI_LOAD_ERROR;
		goto close_volume;
	}

	espeak_ng_InitializePath("/voice");
	espeak_ng_STATUS result = espeak_ng_Initialize(NULL);
	if (result == ENS_OK)
		result = espeak_ng_InitializeOutput(ENOUTPUT_MODE_SYNCHRONOUS, 0, NULL);
	if (result == ENS_OK)
		result = espeak_ng_SetVoiceByFile("/voice/lang/gmw/en-US");
	source_rate = espeak_ng_GetSampleRate();
	if (result != ENS_OK || !source_rate) {
		printf("eSpeak initialization failed (%x)\n", result);
		status = EFI_DEVICE_ERROR;
		goto terminate;
	}

	espeak_SetSynthCallback(receive_samples);
	status = synthesize(root, L"FIXED.WAV", "Firmware settings. Use the arrow keys to select a boot device.");
	if (!EFI_ERROR(status))
		status = synthesize(root, L"DYNAMIC.WAV", input);

terminate:
	espeak_ng_Terminate();
	release_voice_data();
close_volume:
	root->Close(root);
	printf("Speech prototype returned %lx\n", (unsigned long)status);
	return status;
}
