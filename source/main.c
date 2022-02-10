#include <assert.h>
#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include "log.h"
#include "load.h"

int SceDisplay_init(void);
int SceCtrl_init(void);

int main(int argc, char *argv[])
{
	Jit jit;
	void *entry;

	consoleInit(NULL);
	log_to_fb_console = true;

	LOG("-- vita2hos --");

	int ret = load_exe(&jit, "/test.elf", &entry);
	if (ret == 0) {
		LOG("Jumping to the entry point at %p...", entry);
		/* Close FB console */
		consoleUpdate(NULL);
		log_to_fb_console = false;
		consoleExit(NULL);

		/* Init modules */
		SceCtrl_init();
		SceDisplay_init();

		/* Jump to the entrypoint! */
		((void (*)(int arglen, const void *argp))entry)(0, NULL);

		/* Close the JIT */
		jitClose(&jit);

		/* Open FB console */
		consoleInit(NULL);
		log_to_fb_console = true;

		LOG("Returned!");
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

