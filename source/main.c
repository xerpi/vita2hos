#include <assert.h>
#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <psp2/kernel/threadmgr.h>
#include "SceSysmem.h"
#include "SceKernelThreadMgr.h"
#include "SceLibKernel.h"
#include "SceCtrl.h"
#include "SceDisplay.h"
#include "SceGxm.h"
#include "SceTouch.h"
#include "module.h"
#include "log.h"
#include "load.h"

static void register_modules(void)
{
	SceSysmem_register();
	SceLibKernel_register();
	SceKernelThreadMgr_register();
	SceDisplay_register();
	SceGxm_register();
	SceCtrl_register();
	SceTouch_register();
}

static int launch(SceKernelThreadEntry entry)
{
	int ret;

	/* Init modules */
	ret = SceSysmem_init();
	if (ret != 0)
		goto done;
	ret = SceLibKernel_init();
	if (ret != 0)
		goto done;
	ret = SceKernelThreadMgr_init();
	if (ret != 0)
		goto done;
	ret = SceDisplay_init();
	if (ret != 0)
		goto done;
	ret = SceGxm_init();
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

	SceDisplay_finish();
	SceKernelThreadMgr_finish();

	LOG("Process finished! Returned: %d", ret);

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

	register_modules();

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

	module_finish();

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

