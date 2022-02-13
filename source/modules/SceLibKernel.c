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
#include "utils.h"
#include "log.h"

#define MAX_OPENED_DIRS 32

typedef struct {
	SceUID fd;
	DIR *dir;
	char *path;
} VitaOpenedDir;

static UEvent *g_process_exit_event_ptr;
static int *g_process_exit_res_ptr;

static VitaOpenedDir g_vita_opened_dirs[MAX_OPENED_DIRS];
static bool g_vita_opened_dirs_valid[MAX_OPENED_DIRS];

static VitaOpenedDir *opened_dir_alloc(void)
{
	for (int i = 0; i < ARRAY_SIZE(g_vita_opened_dirs); i++) {
		if (!g_vita_opened_dirs_valid[i]) {
			g_vita_opened_dirs_valid[i] = true;
			return &g_vita_opened_dirs[i];
		}
	}

	return NULL;
}

static VitaOpenedDir *get_opened_dir_for_fd(SceUID fd)
{
	for (int i = 0; i < ARRAY_SIZE(g_vita_opened_dirs); i++) {
		if (g_vita_opened_dirs_valid[i] && g_vita_opened_dirs[i].fd == fd) {
			return &g_vita_opened_dirs[i];
		}
	}

	return NULL;
}

int SceLibKernel_init(UEvent *process_exit_event_ptr, int *process_exit_res_ptr)
{
	g_process_exit_event_ptr = process_exit_event_ptr;
	g_process_exit_res_ptr = process_exit_res_ptr;
	return 0;
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

	*g_process_exit_res_ptr = res;
	ueventSignal(g_process_exit_event_ptr);

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

int sceIoDclose(SceUID fd)
{
	VitaOpenedDir *vdir = get_opened_dir_for_fd(fd);

	if (!vdir || !closedir(vdir->dir))
		return SCE_ERROR_ERRNO_EBADF;

	free(vdir->path);
	//put(vdir);

	return 0;
}