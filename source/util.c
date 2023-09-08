#include <errno.h>
#include <stdlib.h>
#include "log.h"
#include "util.h"

int util_load_file(const char *filename, void **data, uint32_t *size)
{
	FILE *fp;

	fp = fopen(filename, "rb");
	if (!fp) {
		LOG("util_load_file: could not open file \"%s\". Error: %s", filename,
		    strerror(errno));
		return -1;
	}

	fseek(fp, 0, SEEK_END);
	*size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	*data = malloc(*size);
	if (!*data) {
		LOG("util_load_file: could not allocate memory");
		fclose(fp);
		return -1;
	}

	if (fread(*data, 1, *size, fp) != *size) {
		LOG("util_load_file: could not read the file");
		free(*data);
		fclose(fp);
		return -1;
	}

	fclose(fp);

	return 0;
}

int util_write_binary_file(const char *filename, const void *data, uint32_t size)
{
	FILE *fp;

	fp = fopen(filename, "wb");
	if (!fp)
		return -1;

	fwrite(data, 1, size, fp);
	fclose(fp);

	return 0;
}

int util_write_text_file(const char *filename, const char *data)
{
	FILE *fp;

	fp = fopen(filename, "w");
	if (!fp)
		return -1;

	fputs(data, fp);
	fclose(fp);

	return 0;
}
