#include <assert.h>
#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <psp2/kernel/threadmgr.h>
#include "SceKernelThreadMgr.h"
#include "SceLibKernel.h"
#include "SceCtrl.h"
#include "SceDisplay.h"
#include "SceTouch.h"
#include "log.h"
#include "load.h"

static int launch(SceKernelThreadEntry *entry)
{
	UEvent process_exit_event;
	int process_exit_res;
	Result res;
	int ret;

	ueventCreate(&process_exit_event, false);

	/* Init modules */
	ret = SceLibKernel_init(&process_exit_event, &process_exit_res);
	if (ret != 0)
		goto done;
	ret = SceKernelThreadMgr_init();
	if (ret != 0)
		goto done;
	ret = SceDisplay_init();
	if (ret != 0)
		goto done;
	ret = SceCtrl_init();
	if (ret != 0)
		goto done;
	ret = SceTouch_init();
	if (ret != 0)
		goto done;

	LOG("Entering SceKernelThreadMgr...");

	ret = SceKernelThreadMgr_main_entry(entry, 0, NULL);

	LOG("Waiting for process termination...");

	res = waitSingle(waiterForUEvent(&process_exit_event), -1);
	if (R_FAILED(res)) {
		LOG("Error to wait for : 0x%lx", res);
		ret = -1;
		goto done;
	}

	LOG("Process finished! Returned: %d", process_exit_res);

	ret = process_exit_res;

done:

	return ret;
}

int main(int argc, char *argv[])
{
	Jit jit;
	void *entry;
	int ret;

	consoleInit(NULL);
	log_to_fb_console = true;

	LOG("-- vita2hos --");

	ret = load_exe(&jit, "/test.elf", &entry);
	if (ret == 0) {
		/* Close FB console */
		consoleUpdate(NULL);
		log_to_fb_console = false;
		consoleExit(NULL);

		LOG("Launching PSVita executable!");

		/* Jump to Vita's ELF entrypoint */
		ret = launch(entry);

		/* Open FB console */
		consoleInit(NULL);
		log_to_fb_console = true;

		LOG("Returned from launch with result: %d", ret);

		/* Close the JIT */
		ret = jitClose(&jit);
		LOG("jitClose() returned: 0x%x", ret);
	} else {
		LOG("Error loading ELF");
	}

	while (appletMainLoop()) {
		/*padUpdate(&pad);
		u64 kDown = padGetButtonsDown(&pad);
		if (kDown & HidNpadButton_Plus)
			break;*/

		consoleUpdate(NULL);
	}

	consoleExit(NULL);
	return 0;
}

