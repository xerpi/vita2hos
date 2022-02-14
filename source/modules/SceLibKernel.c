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
#include "bitset.h"
#include "utils.h"
#include "log.h"

#define MAX_OPENED_FILES 32
#define MAX_OPENED_DIRS 32

typedef struct {
	uint32_t index;
	SceUID fd;
	FILE *fp;
	char *file;
} VitaOpenedFile;

typedef struct {
	uint32_t index;
	SceUID fd;
	DIR *dir;
	char *path;
} VitaOpenedDir;

static UEvent g_process_exit_event;
static atomic_int g_process_exit_res;

static BITSET_DEFINE(g_vita_opened_files_valid, MAX_OPENED_FILES);
static Mutex g_vita_opened_files_mutex;
static VitaOpenedFile g_vita_opened_files[MAX_OPENED_FILES];

static BITSET_DEFINE(g_vita_opened_dirs_valid, MAX_OPENED_DIRS);
static Mutex g_vita_opened_dirs_mutex;
static VitaOpenedDir g_vita_opened_dirs[MAX_OPENED_DIRS];

static VitaOpenedFile *opened_file_alloc(void)
{
	uint32_t index;

	mutexLock(&g_vita_opened_files_mutex);
	index = bitset_find_first_clear_and_set(g_vita_opened_files_valid);
	mutexUnlock(&g_vita_opened_files_mutex);

	if (index == UINT32_MAX)
		return NULL;

	g_vita_opened_files[index].index = index;

	return &g_vita_opened_files[index];
}

static void opened_file_release(VitaOpenedFile *file)
{
	mutexLock(&g_vita_opened_files_mutex);
	BITSET_CLEAR(g_vita_opened_files_valid, file->index);
	mutexUnlock(&g_vita_opened_files_mutex);
}

static VitaOpenedFile *get_opened_file_for_fd(SceUID fd)
{
	mutexLock(&g_vita_opened_files_mutex);
	bitset_for_each_bit_set(g_vita_opened_files_valid, index) {
		if (g_vita_opened_files[index].fd == fd) {
			mutexUnlock(&g_vita_opened_files_mutex);
			return &g_vita_opened_files[index];
		}
	}
	mutexUnlock(&g_vita_opened_files_mutex);
	return NULL;
}

static VitaOpenedDir *opened_dir_alloc(void)
{
	uint32_t index;

	mutexLock(&g_vita_opened_dirs_mutex);
	index = bitset_find_first_clear_and_set(g_vita_opened_dirs_valid);
	mutexUnlock(&g_vita_opened_dirs_mutex);

	if (index == UINT32_MAX)
		return NULL;

	g_vita_opened_dirs[index].index = index;

	return &g_vita_opened_dirs[index];
}

static void opened_dir_release(VitaOpenedDir *dir)
{
	mutexLock(&g_vita_opened_dirs_mutex);
	BITSET_CLEAR(g_vita_opened_dirs_valid, dir->index);
	mutexUnlock(&g_vita_opened_dirs_mutex);
}

static VitaOpenedDir *get_opened_dir_for_fd(SceUID fd)
{
	mutexLock(&g_vita_opened_dirs_mutex);
	bitset_for_each_bit_set(g_vita_opened_dirs_valid, index) {
		if (g_vita_opened_dirs[index].fd == fd) {
			mutexUnlock(&g_vita_opened_dirs_mutex);
			return &g_vita_opened_dirs[index];
		}
	}
	mutexUnlock(&g_vita_opened_dirs_mutex);
	return NULL;
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
	return 0;
}

int sceKernelDeleteLwMutex(SceKernelLwMutexWork *pWork)
{
	return 0;
}

int sceKernelLockLwMutex(SceKernelLwMutexWork *pWork, int lockCount, unsigned int *pTimeout)
{
	return 0;
}

int sceKernelTryLockLwMutex(SceKernelLwMutexWork *pWork, int lockCount)
{
	return 0;
}

int sceKernelUnlockLwMutex(SceKernelLwMutexWork *pWork, int unlockCount)
{
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

	vfile->fd = SceSysmem_get_next_uid();
	vfile->fp = fp;
	vfile->file = strdup(file);

	return vfile->fd;
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

	vdir->fd = SceSysmem_get_next_uid();
	vdir->dir = dir;
	vdir->path = strdup(dirname);

	return vdir->fd;
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
