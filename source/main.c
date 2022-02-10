#include <assert.h>
#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include "SceKernelThreadMgr.h"
#include "SceCtrl.h"
#include "SceDisplay.h"
#include "log.h"
#include "load.h"

static int launch(const void *entry)
{
	int ret;

	/* Init modules */
	ret = SceKernelThreadMgr_init();
	if (ret != 0)
		return ret;
	ret = SceCtrl_init();
	if (ret != 0)
		return ret;
	ret = SceDisplay_init();
	if (ret != 0)
		return ret;

	LOG("Jumping to the entry point at %p...", entry);
	ret = ((int (*)(int arglen, const void *argp))entry)(0, NULL);

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

		/* Jump to Vita's ELF entrypoint */
		ret = launch(entry);

		/* Close the JIT */
		jitClose(&jit);

		/* Open FB console */
		consoleInit(NULL);
		log_to_fb_console = true;

		LOG("Returned! Return value: 0x%x", ret);
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

