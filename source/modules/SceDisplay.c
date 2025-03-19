#include <deko3d.h>
#include <psp2/display.h>
#include <stdbool.h>
#include <stdlib.h>
#include <switch.h>

#include "modules/SceDisplay.h"
#include "dk_helpers.h"
#include "log.h"
#include "module.h"
#include "util.h"

#include "display/display_to_dk.h"
#include "modules/SceSysmem.h"

#define SWAPCHAIN_SIZE 2
#define CMDBUF_SIZE    4 * 1024

static DkDevice g_dk_device;
static DkQueue g_transfer_queue;
static DkMemBlock g_cmdbuf_memblock;
static DkCmdBuf g_cmdbuf;
static DkMemBlock g_framebuffer_memblock;
static DkImage g_swapchain_images[SWAPCHAIN_SIZE];
static DkSwapchain g_swapchain;
static bool g_swapchain_created;
static uint32_t g_swapchain_image_width;
static uint32_t g_swapchain_image_height;
static Thread g_presenter_thread;
static LEvent g_presenter_thread_run;
static Mutex g_vblank_mutex;
static CondVar g_vblank_condvar;
static SceDisplayFrameBuf g_vita_conf_fb;

static void create_swapchain(uint32_t width, uint32_t height)
{
    DkImageLayoutMaker image_layout_maker;
    DkImageLayout framebuffer_layout;
    DkSwapchainMaker swapchain_maker;
    DkImage const *swapchain_images_ptrs[SWAPCHAIN_SIZE];
    uint32_t framebuffer_size, framebuffer_align;

    /* Calculate layout for the framebuffers */
    dkImageLayoutMakerDefaults(&image_layout_maker, g_dk_device);
    image_layout_maker.flags = DkImageFlags_Usage2DEngine | DkImageFlags_UsagePresent;
    image_layout_maker.format = DkImageFormat_RGBA8_Unorm;
    image_layout_maker.dimensions[0] = width;
    image_layout_maker.dimensions[1] = height;
    dkImageLayoutInitialize(&framebuffer_layout, &image_layout_maker);

    /* Retrieve necessary size and alignment for the framebuffers */
    framebuffer_size = dkImageLayoutGetSize(&framebuffer_layout);
    framebuffer_align = dkImageLayoutGetAlignment(&framebuffer_layout);
    framebuffer_size = ALIGN(framebuffer_size, framebuffer_align);

    /* Create a memory block that will host the framebuffers */
    g_framebuffer_memblock = dk_alloc_memblock(g_dk_device, SWAPCHAIN_SIZE * framebuffer_size,
                                               DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);

    /* Initialize the framebuffers with the layout and backing memory we've just created */
    for (uint32_t i = 0; i < SWAPCHAIN_SIZE; i++) {
        swapchain_images_ptrs[i] = &g_swapchain_images[i];
        dkImageInitialize(&g_swapchain_images[i], &framebuffer_layout, g_framebuffer_memblock,
                          i * framebuffer_size);
    }

    /* Create a swapchain out of the framebuffers we've just initialized */
    dkSwapchainMakerDefaults(&swapchain_maker, g_dk_device, nwindowGetDefault(),
                             swapchain_images_ptrs, SWAPCHAIN_SIZE);
    g_swapchain = dkSwapchainCreate(&swapchain_maker);

    g_swapchain_image_width = width;
    g_swapchain_image_height = height;
    g_swapchain_created = true;
}

static void cmdbuf_copy_image(DkCmdBuf cmdbuf, DkImage const *src_image, uint32_t src_width,
                              uint32_t src_height, DkImage const *dst_image, uint32_t dst_width,
                              uint32_t dst_height)
{
    DkImageView src_view, dst_view;
    DkImageRect src_rect, dst_rect;

    src_rect.x = src_rect.y = src_rect.z = 0;
    src_rect.width = src_width;
    src_rect.height = src_height;
    src_rect.depth = 1;

    dst_rect.x = dst_rect.y = dst_rect.z = 0;
    dst_rect.width = dst_width;
    dst_rect.height = dst_height;
    dst_rect.depth = 1;

    dkImageViewDefaults(&src_view, src_image);
    dkImageViewDefaults(&dst_view, dst_image);
    dkCmdBufBlitImage(cmdbuf, &src_view, &src_rect, &dst_view, &dst_rect, 0, 1);
}

static bool dkimage_for_existing_framebuffer(DkImage *image, const void *addr, uint32_t width,
                                             uint32_t height, uint32_t stride,
                                             SceDisplayPixelFormat pixelfmt)
{
    DkMemBlock memblock;
    DkImageLayoutMaker image_layout_maker;
    DkImageLayout image_layout;

    memblock = SceSysmem_get_dk_memblock_for_addr(addr);
    if (!memblock)
        return false;

    dkImageLayoutMakerDefaults(&image_layout_maker, g_dk_device);
    image_layout_maker.flags = DkImageFlags_PitchLinear | DkImageFlags_Usage2DEngine;
    image_layout_maker.format = display_pixelformat_to_dk_image_format(pixelfmt);
    image_layout_maker.dimensions[0] = width;
    image_layout_maker.dimensions[1] = height;
    image_layout_maker.pitchStride = stride * display_pixelformat_bytes_per_pixel(pixelfmt);
    dkImageLayoutInitialize(&image_layout, &image_layout_maker);
    dkImageInitialize(image, &image_layout, memblock, dk_memblock_cpu_addr_offset(memblock, addr));

    return true;
}

static void presenter_thread_func(void *arg)
{
    DkCmdList cmdlist;
    DkImage src_image;
    DkFence acquire_fence;
    const void *base;
    uint32_t width, height, stride;
    uint32_t pixelfmt;
    int slot;

    while (leventTryWait(&g_presenter_thread_run)) {
        /* Make sure Vita has configured a framebuffer */
        if (!g_vita_conf_fb.base)
            goto next_frame_sleep;

        base = g_vita_conf_fb.base;
        width = g_vita_conf_fb.width;
        height = g_vita_conf_fb.height;
        stride = g_vita_conf_fb.pitch;
        pixelfmt = g_vita_conf_fb.pixelformat;

        /* Reconfigure if sizes change and create the swapchain for the first time */
        if (!g_swapchain_created) {
            create_swapchain(width, height);
        } else if (g_swapchain_image_width != width || g_swapchain_image_height != height) {
            dkQueueWaitIdle(g_transfer_queue);
            dkSwapchainDestroy(g_swapchain);
            dkMemBlockDestroy(g_framebuffer_memblock);
            create_swapchain(width, height);
        }

        /* Build a DkImage for the source PSVita-configured framebuffer */
        if (!dkimage_for_existing_framebuffer(&src_image, base, width, height, stride, pixelfmt))
            goto next_frame_sleep;

        /* Acquire a framebuffer from the swapchain */
        dkSwapchainAcquireImage(g_swapchain, &slot, &acquire_fence);

        /* Wait for the acquire fence to be signaled before starting the 2D transfer */
        dkCmdBufWaitFence(g_cmdbuf, &acquire_fence);

        /* 2D copy command from the PSVita-configured framebuffer to the swapchain framebuffer */
        cmdbuf_copy_image(g_cmdbuf, &src_image, width, height, &g_swapchain_images[slot], width,
                          height);

        /* Finish the command list */
        cmdlist = dkCmdBufFinishList(g_cmdbuf);

        /* Submit the command list */
        dkQueueSubmitCommands(g_transfer_queue, cmdlist);

        /* Present the new frame once the transfer finishes */
        dkQueuePresentImage(g_transfer_queue, g_swapchain, slot);

        /* Wait until the acquire fence is signalled: the new frame has been presented */
        dkFenceWait(&acquire_fence, -1);

        /* Notify that there has been a "VBlank" (new frame presented) */
        condvarWakeAll(&g_vblank_condvar);

        /* Make sure the transfer has finished (this should have happened when the acquire fence got
         * signalled) */
        dkQueueWaitIdle(g_transfer_queue);

        /* The transfer has finished and the queue is idle, we can reset the command buffer */
        dkCmdBufClear(g_cmdbuf);

        continue;

    next_frame_sleep:
        svcSleepThread(16666667ull);
    }

    threadExit();
}

EXPORT(SceDisplayUser, 0x7A410B64, int, sceDisplaySetFrameBuf, const SceDisplayFrameBuf *pParam,
       SceDisplaySetBufSync sync)
{
    g_vita_conf_fb = *pParam;

    return 0;
}

EXPORT(SceDisplayUser, 0x42AE6BBC, int, sceDisplayGetFrameBuf, SceDisplayFrameBuf *pParam,
       SceDisplaySetBufSync sync)
{
    *pParam = g_vita_conf_fb;

    return 0;
}

EXPORT(SceDisplay, 0x5795E898, int, sceDisplayWaitVblankStart, void)
{
    condvarWait(&g_vblank_condvar, &g_vblank_mutex);
    return 0;
}

DECLARE_LIBRARY(SceDisplay, 0x5ed8f994);
DECLARE_LIBRARY(SceDisplayUser, 0x4faacd11);

int SceDisplay_init(DkDevice dk_device)
{
    DkQueueMaker queue_maker;
    DkCmdBufMaker cmdbuf_maker;
    Result res;

    g_dk_device = dk_device;

    /* Create graphics queue for transfers */
    dkQueueMakerDefaults(&queue_maker, dk_device);
    queue_maker.flags = DkQueueFlags_HighPrio;
    queue_maker.perWarpScratchMemorySize = 0;
    queue_maker.maxConcurrentComputeJobs = 0;
    g_transfer_queue = dkQueueCreate(&queue_maker);

    /* Create a memory block which will be used for recording command lists using a command buffer
     */
    g_cmdbuf_memblock = dk_alloc_memblock(g_dk_device, CMDBUF_SIZE,
                                          DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);

    /* Create a command buffer object */
    dkCmdBufMakerDefaults(&cmdbuf_maker, g_dk_device);
    g_cmdbuf = dkCmdBufCreate(&cmdbuf_maker);

    /* Feed our memory to the command buffer so that we can start recording commands */
    dkCmdBufAddMemory(g_cmdbuf, g_cmdbuf_memblock, 0, CMDBUF_SIZE);

    leventInit(&g_presenter_thread_run, true, false);
    mutexInit(&g_vblank_mutex);
    condvarInit(&g_vblank_condvar);

    g_swapchain_created = false;

    res = threadCreate(&g_presenter_thread, presenter_thread_func, NULL, NULL, 0x10000, 28, -2);
    if (R_FAILED(res)) {
        LOG("Error creating VSync thread: 0x%" PRIx32, res);
        return res;
    }

    res = threadStart(&g_presenter_thread);
    if (R_FAILED(res)) {
        LOG("Error starting VSync thread: 0x%" PRIx32, res);
        return res;
    }

    return 0;
}

int SceDisplay_finish(void)
{
    Result res;

    leventClear(&g_presenter_thread_run);
    condvarWakeAll(&g_vblank_condvar);

    res = threadWaitForExit(&g_presenter_thread);
    if (R_FAILED(res)) {
        LOG("Error waiting for the presenter thread to finish: 0x%" PRIx32, res);
        return res;
    }

    dkQueueWaitIdle(g_transfer_queue);
    dkCmdBufDestroy(g_cmdbuf);
    dkMemBlockDestroy(g_cmdbuf_memblock);
    if (g_swapchain_created) {
        dkSwapchainDestroy(g_swapchain);
        dkMemBlockDestroy(g_framebuffer_memblock);
    }
    dkQueueDestroy(g_transfer_queue);

    return 0;
}
