#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <dirent.h>
#include <switch.h>
#include <psp2common/types.h>
#include <psp2/kernel/error.h>
#undef st_ctime
#undef st_atime
#undef st_mtime
#include <psp2/io/dirent.h>
#include "SceLibKernel.h"
#include "SceKernelThreadMgr.h"
#include "SceSysmem.h"
#include "module.h"
#include "protected_bitset.h"
#include "utils.h"
#include "log.h"

#define MAX_OPENED_FILES 32
#define MAX_OPENED_DIRS  32

typedef struct {
	uint32_t index;
	SceUID uid;
	FILE *fp;
	char *file;
} VitaOpenedFile;

typedef struct {
	uint32_t index;
	SceUID uid;
	DIR *dir;
	char *path;
} VitaOpenedDir;

static UEvent g_process_exit_event;
static atomic_int g_process_exit_res;

DECL_PROTECTED_BITSET(VitaOpenedFile, vita_opened_files, MAX_OPENED_FILES)
DECL_PROTECTED_BITSET_ALLOC(opened_file_alloc, vita_opened_files, VitaOpenedFile)
DECL_PROTECTED_BITSET_RELEASE(opened_file_release, vita_opened_files, VitaOpenedFile)
DECL_PROTECTED_BITSET_GET_FOR_UID(get_opened_file_for_fd, vita_opened_files, VitaOpenedFile)

DECL_PROTECTED_BITSET(VitaOpenedDir, vita_opened_dirs, MAX_OPENED_DIRS)
DECL_PROTECTED_BITSET_ALLOC(opened_dir_alloc, vita_opened_dirs, VitaOpenedDir)
DECL_PROTECTED_BITSET_RELEASE(opened_dir_release, vita_opened_dirs, VitaOpenedDir)
DECL_PROTECTED_BITSET_GET_FOR_UID(get_opened_dir_for_fd, vita_opened_dirs, VitaOpenedDir)

void *sceKernelGetTLSAddr(int key)
{
	VitaThreadInfo *ti;

	if (key >= 0 && key <= 0x100) {
		ti = SceKernelThreadMgr_get_thread_info();
		return &ti->vita_tls[key];
	}

	return NULL;
}

int sceKernelExitProcess(int res)
{
	LOG("sceKernelExitProcess called! Return value %d", res);

	atomic_store(&g_process_exit_res, res);
	ueventSignal(&g_process_exit_event);

	threadExit();

	return 0;
}

int sceKernelCreateLwMutex(SceKernelLwMutexWork *pWork, const char *pName, unsigned int attr,
			   int initCount, const SceKernelLwMutexOptParam *pOptParam)
{
	Mutex *mutex = (void *)pWork;
	*mutex = initCount;
	return 0;
}

int sceKernelDeleteLwMutex(SceKernelLwMutexWork *pWork)
{
	return 0;
}

int sceKernelLockLwMutex(SceKernelLwMutexWork *pWork, int lockCount, unsigned int *pTimeout)
{
	mutexLock((void *)pWork);
	return 0;
}

int sceKernelTryLockLwMutex(SceKernelLwMutexWork *pWork, int lockCount)
{
	if (mutexTryLock((void *)pWork))
		return 0;
	else
		return SCE_KERNEL_ERROR_LW_MUTEX_FAILED_TO_OWN;
}

int sceKernelUnlockLwMutex(SceKernelLwMutexWork *pWork, int unlockCount)
{
	mutexUnlock((void *)pWork);
	return 0;
}

static const char *vita_io_open_flags_to_fopen_flags(int flags)
{
	if (flags == SCE_O_RDONLY)
		return "r";
	else if (flags == (SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC))
		return "w";
	else if (flags == (SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND))
		return "a";
	else if (flags == SCE_O_RDWR)
		return "r+";
	else if (flags == (SCE_O_RDWR | SCE_O_CREAT | SCE_O_TRUNC))
		return "w+";
	else if (flags == (SCE_O_RDWR | SCE_O_CREAT | SCE_O_APPEND))
		return "a+";
	else
		return NULL;
}

SceUID sceIoOpen(const char *file, int flags, SceMode mode)
{
	VitaOpenedFile *vfile;
	const char *c;
	FILE *fp;

	c = strchr(file, ':');
	if (c)
		file = c + 1;

	if (*file == '\0')
		return SCE_ERROR_ERRNO_ENODEV;

	fp = fopen(file, vita_io_open_flags_to_fopen_flags(flags));
	if (!fp)
		return SCE_ERROR_ERRNO_ENODEV;

	vfile = opened_file_alloc();
	if (!vfile) {
		fclose(fp);
		return SCE_ERROR_ERRNO_EMFILE;
	}

	vfile->uid = SceSysmem_get_next_uid();
	vfile->fp = fp;
	vfile->file = strdup(file);

	return vfile->uid;
}

int sceIoClose(SceUID fd)
{
	VitaOpenedFile *vfile = get_opened_file_for_fd(fd);
	FILE *fp;

	if (!vfile)
		return SCE_ERROR_ERRNO_EBADF;

	fp = vfile->fp;
	free(vfile->file);
	opened_file_release(vfile);

	if (fclose(fp))
		return SCE_ERROR_ERRNO_EBADF;

	return 0;
}

SceSSize sceIoRead(SceUID fd, void *buf, SceSize nbyte)
{
	VitaOpenedFile *vfile = get_opened_file_for_fd(fd);
	if (!vfile)
		return SCE_ERROR_ERRNO_EBADF;

	return fread(buf, 1, nbyte, vfile->fp);
}

SceUID sceIoDopen(const char *dirname)
{
	VitaOpenedDir *vdir;
	const char *c;
	DIR *dir;

	c = strchr(dirname, ':');
	if (c)
		dirname = c + 1;

	dir = opendir(dirname);
	if (!dir)
		return SCE_ERROR_ERRNO_ENODEV;

	vdir = opened_dir_alloc();
	if (!vdir) {
		closedir(dir);
		return SCE_ERROR_ERRNO_EMFILE;
	}

	vdir->uid = SceSysmem_get_next_uid();
	vdir->dir = dir;
	vdir->path = strdup(dirname);

	return vdir->uid;
}

int sceIoDclose(SceUID fd)
{
	VitaOpenedDir *vdir = get_opened_dir_for_fd(fd);
	DIR *dir;

	if (!vdir)
		return SCE_ERROR_ERRNO_EBADF;

	dir = vdir->dir;
	free(vdir->path);
	opened_dir_release(vdir);

	if (closedir(dir))
		return SCE_ERROR_ERRNO_EBADF;

	return 0;
}

static inline void tm_to_sce_datetime(SceDateTime *dt, const struct tm *tm)
{
	dt->year = tm->tm_year;
	dt->month = tm->tm_mon;
	dt->day = tm->tm_mday;
	dt->hour = tm->tm_hour;
	dt->minute = tm->tm_min;
	dt->second = tm->tm_sec;
	dt->microsecond = 0;
}

int sceIoDread(SceUID fd, SceIoDirent *dirent)
{
	VitaOpenedDir *vdir = get_opened_dir_for_fd(fd);
	struct dirent *de;
	struct stat statbuf;
	SceMode st_mode;
	unsigned int st_attr;
	const char *path;
	char full_path[PATH_MAX];

	if (!vdir)
		return SCE_ERROR_ERRNO_EBADF;

	de = readdir(vdir->dir);
	if (!de)
		return 0;

	if (strcmp(vdir->path, "/") == 0) {
		path = de->d_name;
	} else {
		snprintf(full_path, sizeof(full_path), "%s/%s", vdir->path, de->d_name);
		path = full_path;
	}

	if (stat(path, &statbuf) != 0)
		return SCE_ERROR_ERRNO_EBADF;

	if ((statbuf.st_mode & S_IFMT) == S_IFDIR) {
		st_mode = SCE_S_IFDIR | SCE_S_IRWXU | SCE_S_IRWXG | SCE_S_IRWXO;
		st_attr = SCE_SO_IFDIR;
	} else {
		st_mode = SCE_S_IFREG | SCE_S_IRUSR | SCE_S_IWUSR | SCE_S_IRGRP |
			  SCE_S_IWGRP | SCE_S_IROTH | SCE_S_IWOTH;
		st_attr = SCE_SO_IFREG;
	}

	dirent->d_stat.st_mode = st_mode;
	dirent->d_stat.st_attr = st_attr;
	dirent->d_stat.st_size = statbuf.st_size;
	tm_to_sce_datetime(&dirent->d_stat.st_ctime, gmtime(&statbuf.st_ctim.tv_sec));
	tm_to_sce_datetime(&dirent->d_stat.st_atime, gmtime(&statbuf.st_atim.tv_sec));
	tm_to_sce_datetime(&dirent->d_stat.st_mtime, gmtime(&statbuf.st_mtim.tv_sec));
	strncpy(dirent->d_name, de->d_name, sizeof(dirent->d_name));
	dirent->d_private = NULL;

	return 1;
}

void SceLibKernel_register(void)
{
	static const export_entry_t exports[] = {
		{0x7595D9AA, sceKernelExitProcess},
		{0xB295EB61, sceKernelGetTLSAddr},
		{0xDA6EC8EF, sceKernelCreateLwMutex},
		{0x244E76D2, sceKernelDeleteLwMutex},
		{0x46E7BE7B, sceKernelLockLwMutex},
		{0x91FA6614, sceKernelUnlockLwMutex},
		{0x6C60AC61, sceIoOpen},
		{0xF5C6F098, sceIoClose},
		{0xFDB32293, sceIoRead},
		{0xA9283DD0, sceIoDopen},
		{0x9C8B6624, sceIoDread},
		{0x422A221A, sceIoDclose},
	};

	module_register_exports(exports, ARRAY_SIZE(exports));
}

int SceLibKernel_init(void)
{
	ueventCreate(&g_process_exit_event, false);

	return 0;
}

UEvent *SceLibKernel_get_process_exit_uevent(void)
{
	return &g_process_exit_event;
}

int SceLibKernel_get_process_exit_res(void)
{
	return atomic_load(&g_process_exit_res);
}
