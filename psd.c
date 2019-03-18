#include <sys/mman.h>
#include <sys/threads.h>
#include <errno.h>
#include <stdio.h>
#include <usbclient.h>

int main(int argc, char **argv)
{
	// Initialize USB library
	int result = 0;
	if((result = init_usb()) != EOK) {
		printf("Couldn't initialize USB library (%d)\n", result);
	}

	int modn = 0;
	while (dc.op != DC_OP_EXIT) {
		mutexLock(dc.lock);
		while (dc.op == DC_OP_NONE)
			condWait(dc.cond, dc.lock, 0);
		mutexUnlock(dc.lock);

		if (dc.op == DC_OP_RECEIVE) {
			modn = dc.mods_cnt - 1;
			if (modn >= MOD_MAX) {
				printf("dummyfs: Maximum modules number reached (%d), stopping usb...\n", MOD_MAX);
				break;
			} else
				dc.op = DC_OP_NONE;

			dc.op = DC_OP_NONE;
			dc.mods[modn].data = mmap(NULL, (dc.mods[modn].size + 0xfff) & ~0xfff, PROT_WRITE | PROT_READ, MAP_UNCACHED, OID_NULL, 0);
			// Bulk receive
			//dtd_exec_chain(1, dc.mods[modn].data, dc.mods[modn].size, DIR_OUT);
		} else if (dc.op == DC_OP_INIT && bulk_endpt_init() != EOK) {
			dc.op = DC_OP_NONE;
			return 0;
		} else if (dc.op != DC_OP_EXIT)
			dc.op = DC_OP_NONE;
	}

	if (dc.op == DC_OP_EXIT)
		printf("dummyfs: Modules fetched (%d), stopping usb...\n", dc.mods_cnt);

	// Cleanup all USB related data
	destroy_usb();

	//beginthread(exec_modules, 4, &stack, 4096, NULL);

	return EOK;
}
