#include <stdbool.h>
#include <stdlib.h>

#include <switch.h>

#include <psp2/display.h>
#include "utils.h"
#include "log.h"

static Framebuffer g_fb;

int sceDisplaySetFrameBuf(const SceDisplayFrameBuf *pParam, SceDisplaySetBufSync sync)
{
	NWindow *win;
	void *addr;
	u32 stride;
	Result res;

	if (!g_fb.has_init || (g_fb.width_aligned != pParam->width)) {
		win = nwindowGetDefault();
		res = nwindowSetDimensions(win, pParam->width, pParam->height);
		if (R_FAILED(res)) {
			LOG("failed to get window dimensions: 0x%lx", res);
			return -1;
		}

		if (g_fb.has_init)
			framebufferClose(&g_fb);

		res = framebufferCreate(&g_fb, win, pParam->width, pParam->height, PIXEL_FORMAT_RGBA_8888, 2);
		if (R_FAILED(res)) {
			LOG("failed to create fb: 0x%lx", res);
			return -1;
		}

		framebufferMakeLinear(&g_fb);
	}

	addr = framebufferBegin(&g_fb, &stride);
	if (!addr){
		LOG("framebufferBegin returned NULL!");
		return SCE_DISPLAY_ERROR_NO_OUTPUT_SIGNAL;
	}

	memcpy(addr, pParam->base, pParam->height * stride);
	framebufferEnd(&g_fb);

	return 0;
}
