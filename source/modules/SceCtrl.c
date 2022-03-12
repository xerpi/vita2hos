#include <stdbool.h>
#include <stdlib.h>
#include <switch.h>
#include <psp2/ctrl.h>
#include "module.h"
#include "util.h"
#include "log.h"

static PadState g_pad;

int sceCtrlPeekBufferPositive(int port, SceCtrlData *pad_data, int count)
{
	u64 buttons;
	HidAnalogStickState analog_stick_l, analog_stick_r;
	u32 vita_buttons = 0;

	padUpdate(&g_pad);
	buttons = padGetButtons(&g_pad);
	analog_stick_l = padGetStickPos(&g_pad, 0);
	analog_stick_r = padGetStickPos(&g_pad, 1);

	if (buttons & HidNpadButton_Minus)
		vita_buttons |= SCE_CTRL_SELECT;
	if (buttons & HidNpadButton_StickL)
		vita_buttons |= SCE_CTRL_L3;
	if (buttons & HidNpadButton_StickR)
		vita_buttons |= SCE_CTRL_R3;
	if (buttons & HidNpadButton_Plus)
		vita_buttons |= SCE_CTRL_START;
	if (buttons & HidNpadButton_Up)
		vita_buttons |= SCE_CTRL_UP;
	if (buttons & HidNpadButton_Right)
		vita_buttons |= SCE_CTRL_RIGHT;
	if (buttons & HidNpadButton_Down)
		vita_buttons |= SCE_CTRL_DOWN;
	if (buttons & HidNpadButton_Left)
		vita_buttons |= SCE_CTRL_LEFT;
	if (buttons & HidNpadButton_L)
		vita_buttons |= SCE_CTRL_LTRIGGER;
	if (buttons & HidNpadButton_R)
		vita_buttons |= SCE_CTRL_RTRIGGER;
	if (buttons & HidNpadButton_ZL)
		vita_buttons |= SCE_CTRL_L1;
	if (buttons & HidNpadButton_ZR)
		vita_buttons |= SCE_CTRL_R1;
	if (buttons & HidNpadButton_X)
		vita_buttons |= SCE_CTRL_TRIANGLE;
	if (buttons & HidNpadButton_A)
		vita_buttons |= SCE_CTRL_CIRCLE;
	if (buttons & HidNpadButton_B)
		vita_buttons |= SCE_CTRL_CROSS;
	if (buttons & HidNpadButton_Y)
		vita_buttons |= SCE_CTRL_SQUARE;

	pad_data->buttons = vita_buttons;
	pad_data->lx = (255 * (analog_stick_l.x - JOYSTICK_MIN)) / (JOYSTICK_MAX - JOYSTICK_MIN);
	pad_data->ly = (255 * (analog_stick_l.y - JOYSTICK_MIN)) / (JOYSTICK_MAX - JOYSTICK_MIN);
	pad_data->rx = (255 * (analog_stick_r.x - JOYSTICK_MIN)) / (JOYSTICK_MAX - JOYSTICK_MIN);
	pad_data->ry = (255 * (analog_stick_r.y - JOYSTICK_MIN)) / (JOYSTICK_MAX - JOYSTICK_MIN);

	return 0;
}

void SceCtrl_register(void)
{
	static const export_entry_t exports[] = {
		{0xA9C3CED6, sceCtrlPeekBufferPositive},
	};

	module_register_exports(exports, ARRAY_SIZE(exports));
}

int SceCtrl_init(void)
{
	padConfigureInput(1, HidNpadStyleSet_NpadStandard);
	padInitializeAny(&g_pad);

	return 0;
}
