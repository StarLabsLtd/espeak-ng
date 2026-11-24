/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "port.h"
#include <stdint.h>
#include <stdlib.h>
#include <strings.h>

#if COMPRESS_VOICE
#include "LzmaDec.h"
#endif

struct voice_file {
	const char *name;
	size_t offset;
	size_t size;
};
#include "voice_files.h"

extern const unsigned char voice_archive_start[], voice_archive_end[];
static const unsigned char *data;

#if COMPRESS_VOICE
static void *decoder_alloc(void *allocator, size_t size)
{
	(void)allocator;
	return malloc(size);
}

static void decoder_free(void *allocator, void *memory)
{
	(void)allocator;
	free(memory);
}
#endif

bool load_voice_data(void)
{
	if (data)
		return true;

#if COMPRESS_VOICE
	const size_t header_size = 13;
	size_t archive_size = (uintptr_t)voice_archive_end - (uintptr_t)voice_archive_start;
	size_t destination_size = VOICE_DATA_BYTES;
	enum ELzmaStatus end;
	struct ISzAlloc allocator = { decoder_alloc, decoder_free };

	if (archive_size <= header_size)
		return false;
	size_t source_size = archive_size - header_size;
	unsigned char *buffer = malloc(destination_size);
	if (!buffer)
		return false;

	SRes result = LzmaDecode(buffer, &destination_size,
		voice_archive_start + header_size, &source_size,
		voice_archive_start, LZMA_PROPS_SIZE, LZMA_FINISH_END,
		&end, &allocator);
	if (result != SZ_OK || end != LZMA_STATUS_FINISHED_WITH_MARK ||
	    destination_size != VOICE_DATA_BYTES) {
		free(buffer);
		return false;
	}
	data = buffer;
#else
	data = voice_archive_start;
#endif

	return true;
}

void release_voice_data(void)
{
#if COMPRESS_VOICE
	free((void *)data);
#endif
	data = NULL;
}

const unsigned char *voice_file_data(const char *path, size_t *size)
{
	if (!data)
		return NULL;
	for (size_t i = 0; i < sizeof(voice_files) / sizeof(voice_files[0]); i++) {
		if (!strcasecmp(path, voice_files[i].name)) {
			*size = voice_files[i].size;
			return data + voice_files[i].offset;
		}
	}
	return NULL;
}
