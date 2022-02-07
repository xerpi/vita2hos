#include <stdbool.h>
#include <stdlib.h>

#include <switch.h>

#include <psp2/ctrl.h>
#include "utils.h"
#include "log.h"

static PadState g_pad;

int sceCtrlPeekBufferPositive(int port, SceCtrlData *pad_data, int count)
{
	u64 buttons;

	padUpdate(&g_pad);
	buttons = padGetButtons(&g_pad);

	memset(pad_data, 0, sizeof(*pad_data));

	if (buttons & HidNpadButton_Right)
		pad_data->buttons |= SCE_CTRL_RIGHT;
	if (buttons & HidNpadButton_Left)
		pad_data->buttons |= SCE_CTRL_LEFT;
	if (buttons & HidNpadButton_Up)
		pad_data->buttons |= SCE_CTRL_UP;
	if (buttons & HidNpadButton_Down)
		pad_data->buttons |= SCE_CTRL_DOWN;

	pad_data->lx = 128;
	pad_data->ly = 128;
	pad_data->rx = 128;
	pad_data->ry = 128;

	return 0;
}

void SceCtrl_init()
{
	padConfigureInput(1, HidNpadStyleSet_NpadStandard);
	padInitializeAny(&g_pad);
}
