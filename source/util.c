#include <stdlib.h>
#include <switch.h>
#include "log.h"
#include "util.h"

int util_load_file(const char *filename, void **data, uint32_t *size)
{
	FsFileSystem *fs;
	FsFile file;
	void *base;
	s64 filesz;
	u64 bytes_read;;
	Result res;

	fs = fsdevGetDeviceFileSystem("sdmc");
	if (!fs) {
		LOG("load file: error opening SD");
		return -1;
	}

	res = fsFsOpenFile(fs, filename, FsOpenMode_Read, &file);
	if (R_FAILED(res)) {
		LOG("load file: error opening \"%s\": 0x%" PRIx32, filename, res);
		return -1;
	}

	res = fsFileGetSize(&file, &filesz);
	if (R_FAILED(res)) {
		LOG("load file: could not get file size: 0x%" PRIx32, res);
		goto err_close_file;
	}

	base = malloc(filesz);
	if (!base) {
		LOG("load file: could not allocate memory");
		goto err_close_file;
	}

	res = fsFileRead(&file, 0, base, filesz, FsReadOption_None, &bytes_read);
	if (R_FAILED(res)) {
		LOG("load file: could not read the file: 0x%" PRIx32, res);
		goto err_free_data;
	}

	fsFileClose(&file);

	*data = base;
	*size = filesz;

	return 0;

err_free_data:
	free(base);
err_close_file:
	fsFileClose(&file);

	return -1;
}
