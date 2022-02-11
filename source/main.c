#include <assert.h>
#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <psp2/kernel/threadmgr.h>
#include "SceKernelThreadMgr.h"
#include "SceCtrl.h"
#include "SceDisplay.h"
#include "SceTouch.h"
#include "log.h"
#include "load.h"

#define SCE_KERNEL_STACK_SIZE_USER_MAIN		(256 * 1024)
#define SCE_KERNEL_STACK_SIZE_USER_DEFAULT	(4 * 1024)

typedef struct {
	struct {
		SceKernelThreadEntry entry;
		int args;
		void *argp;
	} in;

	struct {
		int ret;
	} out;
} main_thread_trampoline_data_t;

static void NORETURN main_thread_trampoline(void *arg)
{
	main_thread_trampoline_data_t *data = arg;
	int ret;

	/* Init modules */
	ret = SceKernelThreadMgr_init();
	if (ret != 0)
		goto thread_exit;
	ret = SceDisplay_init();
	if (ret != 0)
		goto thread_exit;
	ret = SceCtrl_init();
	if (ret != 0)
		goto thread_exit;
	ret = SceTouch_init();
	if (ret != 0)
		goto thread_exit;

	data->out.ret = data->in.entry(data->in.args, data->in.argp);

thread_exit:
	threadExit();
}

static int launch(const void *entry)
{
	main_thread_trampoline_data_t data;
	Thread main_thread;
	Result res;

	data.in.entry = (SceKernelThreadEntry)entry;
	data.in.args = 0;
	data.in.argp = NULL;

	res = threadCreate(&main_thread, main_thread_trampoline, &data, NULL,
			   SCE_KERNEL_STACK_SIZE_USER_MAIN, 0x3B, -2);
	if (R_FAILED(res)) {
		LOG("Error creating Main thread: 0x%lx", res);
		return res;
	}

	res = threadStart(&main_thread);
	if (R_FAILED(res)) {
		LOG("Error starting Main thread: 0x%lx", res);
		return res;
	}

	LOG("Main thread started! Waiting for it to finish...");

	threadWaitForExit(&main_thread);
	threadClose(&main_thread);

	LOG("Main thread finished! Returned: %d", data.out.ret);

	return data.out.ret;
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

