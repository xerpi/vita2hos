#include <stdbool.h>
#include <stdlib.h>
#include <switch.h>
#include <psp2/display.h>
#include "utils.h"
#include "log.h"

static Thread g_vsync_thread;
static LEvent g_vsync_thread_run;
static Framebuffer g_fb;
static SceDisplayFrameBuf g_vita_conf_fb;

static void vsync_thread_func(void *arg)
{
	NWindow *win;
	void *addr;
	u32 stride;
	Result res;

	while (leventTryWait(&g_vsync_thread_run)) {
		/* Make sure Vita has configured a framebuffer */
		if (!g_vita_conf_fb.base)
			goto next_frame_sleep;

		/* Reconfigure / create the framebuffer for the first time */
		if (!g_fb.has_init || (g_fb.width_aligned != g_vita_conf_fb.width)) {
			win = nwindowGetDefault();
			res = nwindowSetDimensions(win, g_vita_conf_fb.width, g_vita_conf_fb.height);
			if (R_FAILED(res)) {
				LOG("failed to get window dimensions: 0x%lx", res);
				goto next_frame_sleep;
			}

			if (g_fb.has_init)
				framebufferClose(&g_fb);

			res = framebufferCreate(&g_fb, win, g_vita_conf_fb.width,
						g_vita_conf_fb.height, PIXEL_FORMAT_RGBA_8888, 2);
			if (R_FAILED(res)) {
				LOG("failed to create fb: 0x%lx", res);
				goto next_frame_sleep;
			}

			framebufferMakeLinear(&g_fb);
		}

		addr = framebufferBegin(&g_fb, &stride);
		if (!addr){
			LOG("framebufferBegin returned NULL!");
			goto next_frame_sleep;
		}

		memcpy(addr, g_vita_conf_fb.base, g_vita_conf_fb.height * stride);
		framebufferEnd(&g_fb);
		continue;

next_frame_sleep:
		svcSleepThread(16666667ull);
	}

	threadExit();
}

int sceDisplaySetFrameBuf(const SceDisplayFrameBuf *pParam, SceDisplaySetBufSync sync)
{
	g_vita_conf_fb = *pParam;

	return 0;
}

int SceDisplay_init(void)
{
	Result res;

	leventInit(&g_vsync_thread_run, true, false);

	res = threadCreate(&g_vsync_thread, vsync_thread_func, NULL, NULL, 0x10000, 28, -2);
	if (R_FAILED(res)) {
		LOG("Error creating VSync thread: 0x%lx", res);
		return res;
	}

	res = threadStart(&g_vsync_thread);
	if (R_FAILED(res)) {
		LOG("Error starting VSync thread: 0x%lx", res);
		return res;
	}

	return 0;
}

int SceDisplay_finish(void)
{
	Result res;

	LOG("SceDisplay_finish");

	leventClear(&g_vsync_thread_run);

	res = threadWaitForExit(&g_vsync_thread);
	if (R_FAILED(res)) {
		LOG("Error waiting for VSync thread to finish: 0x%lx", res);
		return res;
	}

	return 0;
}
