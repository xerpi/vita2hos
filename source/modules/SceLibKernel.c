#include <dirent.h>
#include <errno.h>
#include <m-dict.h>
#include <psp2/kernel/error.h>
#include <psp2common/types.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <switch.h>
#undef st_ctime
#undef st_atime
#undef st_mtime
#include <psp2/io/dirent.h>

#include "log.h"
#include "module.h"
#include "util.h"

#include "modules/SceKernelThreadMgr.h"
#include "modules/SceLibKernel.h"
#include "modules/SceSysmem.h"

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

DICT_DEF2(vita_opened_files_dict, SceUID, M_DEFAULT_OPLIST, VitaOpenedFile *, M_POD_OPLIST)
static vita_opened_files_dict_t g_vita_opened_files;
static RwLock g_vita_opened_files_lock;

DICT_DEF2(vita_opened_dirs_dict, SceUID, M_DEFAULT_OPLIST, VitaOpenedDir *, M_POD_OPLIST)
static vita_opened_dirs_dict_t g_vita_opened_dirs;
static RwLock g_vita_opened_dirs_lock;

static VitaOpenedFile *get_opened_file_for_fd(SceUID fd)
{
    VitaOpenedFile *vfile;

    rwlockReadLock(&g_vita_opened_files_lock);
    vfile = *vita_opened_files_dict_get(g_vita_opened_files, fd);
    rwlockReadUnlock(&g_vita_opened_files_lock);

    return vfile;
}

static VitaOpenedDir *get_opened_dir_for_fd(SceUID fd)
{
    VitaOpenedDir *vdir;

    rwlockReadLock(&g_vita_opened_dirs_lock);
    vdir = *vita_opened_dirs_dict_get(g_vita_opened_dirs, fd);
    rwlockReadUnlock(&g_vita_opened_dirs_lock);

    return vdir;
}

EXPORT(SceLibKernel, 0xB295EB61, void *, sceKernelGetTLSAddr, int key)
{
    VitaThreadInfo *ti;

    if (key >= 0 && key <= 0x100) {
        ti = SceKernelThreadMgr_get_thread_info();
        return &ti->vita_tls[key];
    }

    return NULL;
}

EXPORT(SceLibKernel, 0x7595D9AA, int, sceKernelExitProcess, int res)
{
    LOG("sceKernelExitProcess called! Return value %d", res);

    atomic_store(&g_process_exit_res, res);
    ueventSignal(&g_process_exit_event);

    threadExit();

    return 0;
}

EXPORT(SceLibKernel, 0xDA6EC8EF, int, sceKernelCreateLwMutex, SceKernelLwMutexWork *pWork,
       const char *pName, unsigned int attr, int initCount,
       const SceKernelLwMutexOptParam *pOptParam)
{
    Mutex *mutex = (void *)pWork;
    mutexInit(mutex);
    if (initCount > 0)
        mutexLock(mutex);
    return 0;
}

EXPORT(SceLibKernel, 0x244E76D2, int, sceKernelDeleteLwMutex, SceKernelLwMutexWork *pWork)
{
    mutexUnlock((void *)pWork);
    return 0;
}

EXPORT(SceLibKernel, 0x46E7BE7B, int, sceKernelLockLwMutex, SceKernelLwMutexWork *pWork,
       int lockCount, unsigned int *pTimeout)
{
    mutexLock((void *)pWork);
    return 0;
}

EXPORT(SceLibKernel, 0xA6A2C915, int, sceKernelTryLockLwMutex, SceKernelLwMutexWork *pWork,
       int lockCount)
{
    if (mutexTryLock((void *)pWork))
        return 0;
    else
        return SCE_KERNEL_ERROR_LW_MUTEX_FAILED_TO_OWN;
}

EXPORT(SceLibKernel, 0x91FA6614, int, sceKernelUnlockLwMutex, SceKernelLwMutexWork *pWork,
       int unlockCount)
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

EXPORT(SceLibKernel, 0x6C60AC61, SceUID, sceIoOpen, const char *file, int flags, SceMode mode)
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

    vfile = malloc(sizeof(*vfile));
    if (!vfile) {
        fclose(fp);
        return SCE_ERROR_ERRNO_EMFILE;
    }

    memset(vfile, 0, sizeof(*vfile));
    vfile->uid  = SceSysmem_get_next_uid();
    vfile->fp   = fp;
    vfile->file = strdup(file);

    rwlockWriteLock(&g_vita_opened_files_lock);
    vita_opened_files_dict_set_at(g_vita_opened_files, vfile->uid, vfile);
    rwlockWriteUnlock(&g_vita_opened_files_lock);

    return vfile->uid;
}

EXPORT(SceIofilemgr, 0xC70B8886, int, sceIoClose, SceUID fd)
{
    VitaOpenedFile *vfile = get_opened_file_for_fd(fd);
    int ret;

    if (!vfile)
        return SCE_ERROR_ERRNO_EBADF;

    ret = fclose(vfile->fp);
    rwlockWriteLock(&g_vita_opened_files_lock);
    vita_opened_files_dict_erase(g_vita_opened_files, fd);
    rwlockWriteUnlock(&g_vita_opened_files_lock);
    free(vfile->file);
    free(vfile);

    if (ret)
        return SCE_ERROR_ERRNO_EBADF;

    return 0;
}

EXPORT(SceIofilemgr, 0xFDB32293, SceSSize, sceIoRead, SceUID fd, void *buf, SceSize nbyte)
{
    VitaOpenedFile *vfile = get_opened_file_for_fd(fd);
    if (!vfile)
        return SCE_ERROR_ERRNO_EBADF;

    return fread(buf, 1, nbyte, vfile->fp);
}

EXPORT(SceIofilemgr, 0x34EFD876, SceSSize, sceIoWrite, SceUID fd, const void *buf, SceSize nbyte)
{
    VitaOpenedFile *vfile = get_opened_file_for_fd(fd);
    if (!vfile)
        return SCE_ERROR_ERRNO_EBADF;

    return fwrite(buf, 1, nbyte, vfile->fp);
}

EXPORT(SceIofilemgr, 0x49252B9B, long, sceIoLseek32, SceUID fd, long offset, int whence)
{
    VitaOpenedFile *vfile = get_opened_file_for_fd(fd);
    if (!vfile)
        return SCE_ERROR_ERRNO_EBADF;

    return fseek(vfile->fp, offset, whence);
}

EXPORT(SceIofilemgr, 0x16512F59, int, sceIoSyncByFd, SceUID fd, int flag)
{
    VitaOpenedFile *vfile = get_opened_file_for_fd(fd);
    if (!vfile)
        return SCE_ERROR_ERRNO_EBADF;

    return fflush(vfile->fp);
}

EXPORT(SceLibKernel, 0xA9283DD0, SceUID, sceIoDopen, const char *dirname)
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

    vdir = malloc(sizeof(*vdir));
    if (!vdir) {
        closedir(dir);
        return SCE_ERROR_ERRNO_EMFILE;
    }

    memset(vdir, 0, sizeof(*vdir));
    vdir->uid  = SceSysmem_get_next_uid();
    vdir->dir  = dir;
    vdir->path = strdup(dirname);

    rwlockWriteLock(&g_vita_opened_dirs_lock);
    vita_opened_dirs_dict_set_at(g_vita_opened_dirs, vdir->uid, vdir);
    rwlockWriteUnlock(&g_vita_opened_dirs_lock);

    return vdir->uid;
}

EXPORT(SceIofilemgr, 0x422A221A, int, sceIoDclose, SceUID fd)
{
    VitaOpenedDir *vdir = get_opened_dir_for_fd(fd);
    int ret;

    if (!vdir)
        return SCE_ERROR_ERRNO_EBADF;

    ret = closedir(vdir->dir);
    free(vdir->path);
    rwlockWriteLock(&g_vita_opened_dirs_lock);
    vita_opened_dirs_dict_erase(g_vita_opened_dirs, fd);
    rwlockWriteUnlock(&g_vita_opened_dirs_lock);
    free(vdir);

    if (ret)
        return SCE_ERROR_ERRNO_EBADF;

    return 0;
}

static inline void tm_to_sce_datetime(SceDateTime *dt, const struct tm *tm)
{
    dt->year        = tm->tm_year;
    dt->month       = tm->tm_mon;
    dt->day         = tm->tm_mday;
    dt->hour        = tm->tm_hour;
    dt->minute      = tm->tm_min;
    dt->second      = tm->tm_sec;
    dt->microsecond = 0;
}

EXPORT(SceLibKernel, 0x9C8B6624, int, sceIoDread, SceUID fd, SceIoDirent *dirent)
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
        st_mode = SCE_S_IFDIR | SCE_S_IRWXU | SCE_S_IRWXG | SCE_S_IRWXS;
        st_attr = SCE_SO_IFDIR;
    } else {
        st_mode = SCE_S_IFREG | SCE_S_IRUSR | SCE_S_IWUSR | SCE_S_IRGRP | SCE_S_IWGRP |
                  SCE_S_IRSYS | SCE_S_IWSYS;
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

EXPORT_FUNC(SceLibKernel, 0x632980D7, memset)
EXPORT_FUNC(SceLibKernel, 0x14E9DBD7, memcpy)
EXPORT_FUNC(SceLibKernel, 0x736753C8, memmove)
EXPORT_FUNC(SceLibKernel, 0xFA26BC62, printf)

EXPORT(SceLibKernel, 0x0FB972F9, int, sceKernelGetThreadId)
{
    return SceKernelThreadMgr_get_thread_info()->uid;
}

DECLARE_LIBRARY(SceLibKernel, 0xcae9ace6);
DECLARE_LIBRARY(SceIofilemgr, 0xf2ff276e);

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
