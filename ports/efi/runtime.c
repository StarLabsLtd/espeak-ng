/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "port.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <ucd/ucd.h>
#include <wctype.h>

#define NANOPRINTF_IMPLEMENTATION
#define NANOPRINTF_VISIBILITY_STATIC
#include <nanoprintf.h>

static int error_value;
int *__errno_location(void)
{
	return &error_value;
}

union allocation {
	struct {
		void *buffer;
		size_t size;
		union allocation *next;
		union allocation *previous;
	} info;
	max_align_t align;
};
static union allocation *allocations;

void *malloc(size_t size)
{
	const size_t alignment = _Alignof(union allocation);
	const size_t overhead = sizeof(union allocation) + alignment - 1;
	void *buffer;
	if (size > SIZE_MAX - overhead ||
	    EFI_ERROR(boot_services->AllocatePool(EfiBootServicesData, size + overhead, &buffer))) {
		errno = ENOMEM;
		return NULL;
	}
	uintptr_t aligned = ((uintptr_t)buffer + alignment - 1) & ~(alignment - 1);
	union allocation *allocation = (void *)aligned;
	allocation->info.buffer = buffer;
	allocation->info.size = size;
	allocation->info.previous = NULL;
	allocation->info.next = allocations;
	if (allocations)
		allocations->info.previous = allocation;
	allocations = allocation;
	return allocation + 1;
}

void free(void *pointer)
{
	if (pointer) {
		union allocation *allocation = (union allocation *)pointer - 1;
		if (allocation->info.previous)
			allocation->info.previous->info.next = allocation->info.next;
		else
			allocations = allocation->info.next;
		if (allocation->info.next)
			allocation->info.next->info.previous = allocation->info.previous;
		boot_services->FreePool(allocation->info.buffer);
	}
}

void release_speech_allocations(void)
{
	while (allocations)
		free(allocations + 1);
}

void *calloc(size_t count, size_t size)
{
	if (size && count > SIZE_MAX / size) {
		errno = ENOMEM;
		return NULL;
	}
	void *pointer = malloc(count * size);
	if (pointer)
		memset(pointer, 0, count * size);
	return pointer;
}

void *realloc(void *pointer, size_t size)
{
	if (!pointer)
		return malloc(size);
	if (!size) {
		free(pointer);
		return NULL;
	}
	const union allocation *allocation = (const union allocation *)pointer - 1;
	const size_t old_size = allocation->info.size;
	void *replacement = malloc(size);
	if (!replacement)
		return NULL;
	memcpy(replacement, pointer, old_size < size ? old_size : size);
	free(pointer);
	return replacement;
}

static const char *const voice_directories[] = { "/voice", "/voice/lang", "/voice/lang/gmw" };

int stat(const char *path, struct stat *result)
{
	size_t size;
	if (voice_file_data(path, &size)) {
		memset(result, 0, sizeof(*result));
		result->st_mode = S_IFREG;
		result->st_size = size;
		return 0;
	}
	for (size_t i = 0; i < sizeof(voice_directories) / sizeof(voice_directories[0]); i++) {
		if (!strcmp(path, voice_directories[i])) {
			memset(result, 0, sizeof(*result));
			result->st_mode = S_IFDIR;
			return 0;
		}
	}
	errno = ENOENT;
	return -1;
}

struct __dirstream {
	size_t kind;
	size_t position;
	struct dirent entry;
};

DIR *opendir(const char *path)
{
	for (size_t i = 0; i < sizeof(voice_directories) / sizeof(voice_directories[0]); i++) {
		if (strcmp(path, voice_directories[i]))
			continue;
		DIR *directory = calloc(1, sizeof(*directory));
		if (directory)
			directory->kind = i;
		return directory;
	}
	errno = ENOENT;
	return NULL;
}

struct dirent *readdir(DIR *directory)
{
	static const char *const names[][3] = {
		{ "lang", NULL },
		{ "gmw", NULL },
		{ "en", "en-US", NULL },
	};
	const char *name = names[directory->kind][directory->position];
	if (!name)
		return NULL;
	directory->position++;
	strcpy(directory->entry.d_name, name);
	return &directory->entry;
}

int closedir(DIR *directory)
{
	free(directory);
	return 0;
}

struct memory_file {
	const unsigned char *data;
	size_t size;
	size_t position;
};

static struct memory_file diagnostic;
FILE *stderr = (FILE *)&diagnostic;

FILE *fopen(const char *path, const char *mode)
{
	size_t size;
	const unsigned char *data = voice_file_data(path, &size);

	if (!data || (strcmp(mode, "r") && strcmp(mode, "rb"))) {
		errno = ENOENT;
		return NULL;
	}
	struct memory_file *file = malloc(sizeof(*file));
	if (!file)
		return NULL;
	*file = (struct memory_file){ .data = data, .size = size };
	return (FILE *)file;
}

int fclose(FILE *stream)
{
	free(stream);
	return 0;
}

size_t fread(void *output, size_t size, size_t count, FILE *stream)
{
	struct memory_file *file = (void *)stream;
	if (!size || !count)
		return 0;
	size_t available = (file->size - file->position) / size;
	if (count > available)
		count = available;
	memcpy(output, file->data + file->position, size * count);
	file->position += size * count;
	return count;
}

int fseek(FILE *stream, long offset, int origin)
{
	struct memory_file *file = (void *)stream;
	size_t base;
	switch (origin) {
	case SEEK_SET: base = 0; break;
	case SEEK_CUR: base = file->position; break;
	case SEEK_END: base = file->size; break;
	default: errno = EINVAL; return -1;
	}
	if ((offset < 0 && 0UL - (unsigned long)offset > base) ||
	    (offset >= 0 && (unsigned long)offset > file->size - base)) {
		errno = EINVAL;
		return -1;
	}
	file->position = base + offset;
	return 0;
}

int fgetc(FILE *stream)
{
	unsigned char c;
	return fread(&c, 1, 1, stream) == 1 ? c : EOF;
}

char *fgets(char *destination, int capacity, FILE *stream)
{
	if (capacity <= 0)
		return NULL;

	int count = 0;
	while (count < capacity - 1) {
		int c = fgetc(stream);
		if (c == EOF) {
			if (!count)
				return NULL;
			break;
		}
		destination[count++] = c;
		if (c == '\n')
			break;
	}
	destination[count] = 0;
	return destination;
}

int fputc(int c, FILE *stream)
{
	if (stream != stderr)
		return EOF;
	const char text[] = { c, 0 };
	console_write(text);
	return (unsigned char)c;
}

int fprintf(FILE *stream, const char *format, ...)
{
	if (stream != stderr)
		return -1;

	char text[512];
	va_list args;
	va_start(args, format);
	int count = npf_vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	console_write(text);
	return count;
}

int printf(const char *format, ...)
{
	char text[512];
	va_list args;
	va_start(args, format);
	int count = npf_vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	console_write(text);
	return count;
}

int snprintf(char *text, size_t size, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	int count = npf_vsnprintf(text, size, format, args);
	va_end(args);
	return count;
}

int sprintf(char *text, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	int count = npf_vsnprintf(text, SIZE_MAX, format, args);
	va_end(args);
	return count;
}

/* The engine scans integer settings, language names, and a config key. */
int sscanf(const char *text, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	int matched = 0;
	while (*format) {
		if (isspace((unsigned char)*format)) {
			while (isspace((unsigned char)*text))
				text++;
			format++;
			continue;
		}

		if (*format != '%') {
			if (*text != *format)
				break;
			text++;
			format++;
			continue;
		}

		char type = *++format;
		format++;
		if (type != 'c')
			while (isspace((unsigned char)*text))
				text++;
		if (!*text) {
			if (!matched)
				matched = EOF;
			break;
		}

		if (type == 's') {
			char *value = va_arg(args, char *);
			while (*text && !isspace((unsigned char)*text))
				*value++ = *text++;
			*value = 0;
		} else if (type == 'c') {
			*va_arg(args, char *) = *text++;
		} else if (type == 'd' || type == 'x') {
			char *end;
			long value = strtol(text, &end, type == 'x' ? 16 : 10);
			if (end == text)
				break;
			if (type == 'x')
				*va_arg(args, unsigned int *) = (unsigned int)value;
			else
				*va_arg(args, int *) = (int)value;
			text = end;
		} else {
			break;
		}
		matched++;
	}
	va_end(args);
	return matched;
}

char *getenv(const char *name)
{
	(void)name;
	return NULL;
}

char *setlocale(int category, const char *locale)
{
	(void)category;
	(void)locale;
	return "C.UTF-8";
}

time_t time(time_t *destination)
{
	if (destination)
		*destination = 1;
	return 1;
}

int remove(const char *path)
{
	(void)path;
	errno = EROFS;
	return -1;
}

char *tmpnam(char buffer[L_tmpnam])
{
	(void)buffer;
	errno = EROFS;
	return NULL;
}

void __assert_fail(const char *condition, const char *path, unsigned int line, const char *function)
{
	fprintf(stderr, "%s:%u: %s: assertion %s failed\n", path, line, function, condition);
	for (;;)
		__asm__ volatile ("hlt");
}

int iswalnum(wint_t c) { return ucd_isalnum(c); }
int iswalpha(wint_t c) { return ucd_isalpha(c); }
int iswdigit(wint_t c) { return ucd_isdigit(c); }
int iswlower(wint_t c) { return ucd_islower(c); }
int iswpunct(wint_t c) { return ucd_ispunct(c); }
int iswspace(wint_t c) { return ucd_isspace(c); }
int iswupper(wint_t c) { return ucd_isupper(c); }

double wcstod(const wchar_t *text, wchar_t **end)
{
	while (iswspace(*text))
		text++;
	const wchar_t *start = text;
	bool negative = *text == L'-';
	if (negative || *text == L'+')
		text++;

	double value = 0;
	bool digits = false;
	while (*text >= L'0' && *text <= L'9') {
		value = 10 * value + (*text++ - L'0');
		digits = true;
	}
	if (*text == L'.') {
		double scale = 0.1;
		text++;
		while (*text >= L'0' && *text <= L'9') {
			value += scale * (*text++ - L'0');
			scale *= 0.1;
			digits = true;
		}
	}

	if (end)
		*end = (wchar_t *)(digits ? text : start);
	return negative ? -value : value;
}

long strtol(const char *text, char **end, int base)
{
	const char *start = text;
	while (isspace((unsigned char)*text))
		text++;
	bool negative = *text == '-';
	if (negative || *text == '+')
		text++;
	if (base && (base < 2 || base > 36)) {
		errno = EINVAL;
		return 0;
	}
	if ((!base || base == 16) && text[0] == '0' &&
	    (text[1] == 'x' || text[1] == 'X') && isxdigit((unsigned char)text[2])) {
		base = 16;
		text += 2;
	} else if (!base) {
		base = *text == '0' ? 8 : 10;
	}

	unsigned long magnitude = 0;
	const unsigned long limit = negative ? 0UL - LONG_MIN : LONG_MAX;
	const char *first = text;
	bool overflow = false;
	for (; *text; text++) {
		int c = tolower((unsigned char)*text);
		int digit = c >= '0' && c <= '9' ? c - '0' : c - 'a' + 10;
		if (digit < 0 || digit >= base || (c > '9' && c < 'a'))
			break;
		if (magnitude > (limit - digit) / base)
			overflow = true;
		if (!overflow)
			magnitude = magnitude * base + digit;
	}

	if (end)
		*end = (char *)(text == first ? start : text);
	if (overflow) {
		errno = ERANGE;
		return negative ? LONG_MIN : LONG_MAX;
	}
	return negative ? (long)(0UL - magnitude) : (long)magnitude;
}
