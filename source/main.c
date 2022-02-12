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

#define SCE_KERNEL_STACK_SIZE_USER_MAIN		(256 * 1024)
#define SCE_KERNEL_STACK_SIZE_USER_DEFAULT	(4 * 1024)

typedef struct {
	UEvent *process_exit_event_ptr;
	int *process_exit_res_ptr;
	SceKernelThreadEntry entry;
	int args;
	void *argp;
} main_thread_trampoline_data_t;

static void NORETURN main_thread_trampoline(void *arg)
{
	main_thread_trampoline_data_t *data = arg;
	int ret;

	/* Init modules */
	ret = SceLibKernel_init(data->process_exit_event_ptr,
				data->process_exit_res_ptr);
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

	data->entry(data->args, data->argp);

done:
	threadExit();
}

static int launch(const void *entry)
{
	main_thread_trampoline_data_t data;
	UEvent process_exit_event;
	int process_exit_res;
	Thread main_thread;
	Result res;
	int ret;

	ueventCreate(&process_exit_event, false);

	data.process_exit_event_ptr = &process_exit_event;
	data.process_exit_res_ptr = &process_exit_res;
	data.entry = (SceKernelThreadEntry)entry;
	data.args = 0;
	data.argp = NULL;

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

	LOG("Main thread started!");
	LOG("Waiting for process termination...");

	res = waitSingle(waiterForUEvent(&process_exit_event), -1);
	if (R_FAILED(res)) {
		LOG("Error to wait for : 0x%lx", res);
		ret = -1;
		goto done;
	}

	// TODO: Also wait for the rest of the threads to finish?
	threadWaitForExit(&main_thread);

	LOG("Process finished! Returned: %d", process_exit_res);

	ret = process_exit_res;

done:
	threadClose(&main_thread);

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

