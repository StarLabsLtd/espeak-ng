/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "port.h"

#include <errno.h>
#include <espeak-ng/espeak_ng.h>
#include <stdint.h>
#include <string.h>

#define OUTPUT_LIMIT (PCM_RATE * 30U)

static struct {
	pcm_sink_fn *sink;
	void *context;
	EFI_STATUS error;
	uint32_t frames;
	uint32_t phase;
	size_t buffer_frames;
	int16_t buffer[1024][PCM_CHANNELS];
	int16_t previous;
	bool have_sample;
} output;

static unsigned int source_rate;
static bool initialized;
static bool synthesizing;

static EFI_STATUS flush_output(void)
{
	if (output.buffer_frames) {
		output.error = output.sink(output.context, &output.buffer[0][0],
			output.buffer_frames);
		output.buffer_frames = 0;
	}
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
		if (output.frames == OUTPUT_LIMIT) {
			output.error = EFI_BAD_BUFFER_SIZE;
			return;
		}
		int32_t distance = (int32_t)sample - output.previous;
		int16_t value = output.previous +
			(int64_t)distance * output.phase / PCM_RATE;
		output.buffer[output.buffer_frames][0] = value;
		output.buffer[output.buffer_frames][1] = value;
		output.buffer_frames++;
		output.phase += source_rate;
		output.frames++;
		if (output.buffer_frames == sizeof(output.buffer) / sizeof(output.buffer[0]) &&
		    flush_output() != EFI_SUCCESS)
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
		if (output.error != EFI_SUCCESS)
			return 1;
	}
	return 0;
}

static EFI_STATUS speech_initialize(void)
{
	if (initialized)
		return EFI_SUCCESS;
	if (!load_voice_data())
		return EFI_LOAD_ERROR;

	espeak_ng_InitializePath("/voice");
	espeak_ng_STATUS result = espeak_ng_Initialize(NULL);
	if (result == ENS_OK)
		result = espeak_ng_InitializeOutput(ENOUTPUT_MODE_SYNCHRONOUS, 0, NULL);
	if (result == ENS_OK)
		result = espeak_ng_SetVoiceByFile("/voice/lang/gmw/en-US");
	source_rate = espeak_ng_GetSampleRate();
	if (result != ENS_OK || !source_rate) {
		espeak_ng_Terminate();
		release_voice_data();
		return result == ENOMEM ? EFI_OUT_OF_RESOURCES : EFI_DEVICE_ERROR;
	}

	espeak_SetSynthCallback(receive_samples);
	initialized = true;
	return EFI_SUCCESS;
}

EFI_STATUS speech_render(const char *text, size_t length, pcm_sink_fn *sink, void *context)
{
	if (!text || !sink || !length)
		return EFI_INVALID_PARAMETER;
	if (synthesizing)
		return EFI_ACCESS_DENIED;

	synthesizing = true;
	EFI_STATUS status = speech_initialize();
	if (status != EFI_SUCCESS)
		goto done;

	memset(&output, 0, sizeof(output));
	output.sink = sink;
	output.context = context;
	espeak_ng_STATUS result = espeak_ng_Synthesize(text, length + 1, 0,
		POS_CHARACTER, 0, espeakCHARS_UTF8, NULL, NULL);

	status = output.error;
	if (status == EFI_SUCCESS && result != ENS_OK)
		status = result == ENOMEM ? EFI_OUT_OF_RESOURCES : EFI_DEVICE_ERROR;
	if (status == EFI_SUCCESS && output.have_sample) {
		/* Emit the final source sample for the remainder of its duration. */
		output_sample(output.previous);
		status = output.error;
		if (status == EFI_SUCCESS)
			status = flush_output();
	}
	if (status == EFI_SUCCESS && !output.frames)
		status = EFI_NOT_FOUND;

	output.sink = NULL;
	output.context = NULL;
done:
	synthesizing = false;
	return status;
}

bool speech_busy(void)
{
	return synthesizing;
}

void speech_release(void)
{
	if (initialized) {
		espeak_SetSynthCallback(NULL);
		espeak_ng_Terminate();
		release_voice_data();
		initialized = false;
	}
	release_speech_allocations();
}
