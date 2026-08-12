#include <inttypes.h>
#include <stdio.h>
#include <board_config.h>
#include <unistd.h>

#if defined(__CPU_GR740)


uint64_t getTime(void)
{
	uint32_t asr22, asr23;
	uint64_t cntr;
	__asm__ volatile(
			"rd %%asr22, %0\n\t"
			"rd %%asr23, %1"
			: "=r"(asr22), "=r"(asr23));

	cntr = ((uint64_t)(asr22 & 0xffffffu) << 32) | asr23;

	return cntr;
}


#elif defined(__CPU_ZYNQMP)


uint64_t getTime(void)
{
	uint64_t ticks = 0;
	asm volatile(
			"isb\n"
			"mrs %0, cntpct_el0\n"
			: "=r"(ticks)
			:
			: "memory");
	return ticks;
}


#else


uint64_t getTime(void)
{
	/* TODO: implement per platform
	 * this function needs to work in interrupts!
	 */
	return 0;
}


#endif

int main(void)
{
	uint64_t start = getTime();
	while (getTime() - start < 10000000000)
		;
	printf("done\n");
	return 0;
}
