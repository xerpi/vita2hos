#include <stdbool.h>
#include <stdlib.h>
#include <switch.h>
#include <psp2/touch.h>
#include "module.h"
#include "util.h"
#include "log.h"

#define MAX_TOUCH_STATES 8

int sceTouchPeek(SceUInt32 port, SceTouchData *pData, SceUInt32 nBufs)
{
	HidTouchScreenState states[MAX_TOUCH_STATES];
	int total;

	if (port == SCE_TOUCH_PORT_BACK)
		return 0;

	if (nBufs > MAX_TOUCH_STATES)
		nBufs = MAX_TOUCH_STATES;

	total = hidGetTouchScreenStates(states, nBufs);

	for (int i = 0; i < total; i++) {
		pData[i].timeStamp = states[i].sampling_number;
		pData[i].status = 0;
		pData[i].reportNum = states[i].count;

		for (int j = 0; j < states[i].count; j++) {
			pData[i].report[j].id = states[i].touches[j].finger_id;
			pData[i].report[j].force = 128;
			pData[i].report[j].x = (states[i].touches[j].x * 1920) / 1280;
			pData[i].report[j].y = (states[i].touches[j].y * 1088) / 720;
			pData[i].report[j].info = 0;
		}
	}

	return total;
}

int sceTouchGetPanelInfo(SceUInt32 port, SceTouchPanelInfo *pPanelInfo)
{
	switch (port) {
	case SCE_TOUCH_PORT_FRONT:
		pPanelInfo->minAaX = 0;
		pPanelInfo->minAaY = 0;
		pPanelInfo->maxAaX = 1919;
		pPanelInfo->maxAaY = 1087;
		pPanelInfo->minDispX = 0;
		pPanelInfo->minDispY = 0;
		pPanelInfo->maxDispX = 1919;
		pPanelInfo->maxDispY = 1087;
		pPanelInfo->minForce = 1;
		pPanelInfo->maxForce = 128;
		return 0;
	case SCE_TOUCH_PORT_BACK:
		pPanelInfo->minAaX = 0;
		pPanelInfo->minAaY = 108;
		pPanelInfo->maxAaX = 1919;
		pPanelInfo->maxAaY = 889;
		pPanelInfo->minDispX = 0;
		pPanelInfo->minDispY = 0;
		pPanelInfo->maxDispX = 1919;
		pPanelInfo->maxDispY = 1087;
		pPanelInfo->minForce = 1;
		pPanelInfo->maxForce = 128;
		return 0;
	default:
		return SCE_TOUCH_ERROR_INVALID_ARG;
	}
}

void SceTouch_register(void)
{
	static const export_entry_t exports[] = {
		{0x10A2CA25, sceTouchGetPanelInfo},
		{0xFF082DF0, sceTouchPeek},
	};

	module_register_exports(exports, ARRAY_SIZE(exports));
}

int SceTouch_init(void)
{
	hidInitializeTouchScreen();

	return 0;
}
