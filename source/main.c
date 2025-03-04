#include <assert.h>
#include <arpa/inet.h>
#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <switch.h>
#include <deko3d.h>
#include <psp2/kernel/threadmgr.h>
#include "modules/SceSysmem.h"
#include "modules/SceKernelThreadMgr.h"
#include "modules/SceLibKernel.h"
#include "modules/SceCtrl.h"
#include "modules/SceDisplay.h"
#include "modules/SceGxm.h"
#include "modules/SceTouch.h"
#include "config.h"
#include "module.h"
#include "log.h"
#include "load.h"

static void dk_debug_callback(void *userData, const char *context, DkResult result, const char *message)
{
	char description[256];

	if (result == DkResult_Success) {
		LOG("deko3d debug callback: context: %s, message: %s, result %d",
		    context, message, result);
	} else {
		snprintf(description, sizeof(description), "context: %s, message: %s, result %d",
		         context, message, result);
		fatal_error("deko3d fatal error.", description);
	}
}

static int launch(SceKernelThreadEntry entry)
{
	DkDeviceMaker device_maker;
	DkDevice dk_device;
	int ret;

	/* Initialize deko3d device */
	dkDeviceMakerDefaults(&device_maker);
	device_maker.userData = NULL;
	device_maker.cbDebug = dk_debug_callback;
	dk_device = dkDeviceCreate(&device_maker);

	/* Init modules */
	ret = SceSysmem_init(dk_device);
	if (ret != 0)
		goto done;
	ret = SceLibKernel_init();
	if (ret != 0)
		goto done;
	ret = SceKernelThreadMgr_init();
	if (ret != 0)
		goto done;
	ret = SceDisplay_init(dk_device);
	if (ret != 0)
		goto done;
	ret = SceGxm_init(dk_device);
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

	LOG("SceKernelThreadMgr returned...");

	SceGxm_finish();
	SceDisplay_finish();
	SceKernelThreadMgr_finish();

	dkDeviceDestroy(dk_device);

	LOG("Process finished! Returned: %d", ret);

done:
	return ret;
}

static void create_vita2hos_paths()
{
	mkdir(VITA2HOS_ROOT_PATH, 0755);
	mkdir(VITA2HOS_DUMP_PATH, 0755);
	mkdir(VITA2HOS_DUMP_SHADER_PATH, 0755);
}

int main(int argc, char *argv[])
{
	time_t now;
	char timestr[32];
	Jit jit;
	void *entry;
	int ret;

	consoleDebugInit(debugDevice_SVC);

	socketInitializeDefault();
	inet_pton(AF_INET, "10.42.0.1", &__nxlink_host);
	/* Port 28771 (nc -kl 28771) */
	nxlinkStdio();
	log_to_fb_console = true;

	LOG("vita2hos " VITA2HOS_MAJOR "." VITA2HOS_MINOR "." VITA2HOS_PATCH "-" VITA2HOS_HASH
	    " (" __DATE__ " " __TIME__ ")");

	now = time(NULL);
	strftime(timestr, sizeof(timestr), "%c", localtime(&now));
	LOG("Starting at %s", timestr);

	create_vita2hos_paths();

	ret = load_file(&jit, VITA2HOS_EXE_FILE, &entry);
	if (ret == 0) {
		LOG("Launching PlayStation Vita executable!");

		/* Jump to Vita's executable entrypoint */
		ret = launch(entry);

		LOG("Returned from launch with result: %d", ret);

		/* Close the JIT */
		ret = jitClose(&jit);
		LOG("jitClose() returned: 0x%x", ret);
	} else {
		fatal_error("Error loading PlayStation Vita executable.",
			    "Make sure to place it to '" VITA2HOS_EXE_FILE "'");
	}

	return 0;
}

void NORETURN fatal_error(const char *dialog_message, const char *fullscreen_message)
{
	extern u32 __nx_applet_exit_mode;
	ErrorApplicationConfig c;

	LOG("%s %s", dialog_message, fullscreen_message);

	errorApplicationCreate(&c, dialog_message, fullscreen_message);
	errorApplicationShow(&c);

	__nx_applet_exit_mode = 1;
	exit(1);
}

void NORETURN __assert_func(const char *file, int line, const char *func, const char *failedexpr)
{
	char message[256];

	snprintf(message, sizeof(message), "assertion \"%s\" failed: file \"%s\", line %d%s%s\n",
		failedexpr, file, line, func ? ", function: " : "", func ? func : "");

	fatal_error("Assertion failed.", message);
}
