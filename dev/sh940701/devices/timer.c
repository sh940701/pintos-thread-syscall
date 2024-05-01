#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
static int64_t ticks;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops(unsigned loops);
static void busy_wait(int64_t loops);
static void real_time_sleep(int64_t num, int32_t denom);

/* Sets up the 8254 Programmable Interval Timer (PIT) to
   interrupt PIT_FREQ times per second, and registers the
   corresponding interrupt. */
void timer_init(void)
{
	/* 8254 input frequency divided by TIMER_FREQ, rounded to
	   nearest. */
	// The timer will divide it's input clock of 1.19MHz (1193180Hz) -> 8254 timer 의 clock 값은 1.193180MHz 이다. 즉 1 초에 1193180 번 진동한다.
	// http://www.osdever.net/bkerndev/Docs/pit.htm
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	// 0x43: timer 에게 전달할 제어 명령어를 기록하는 port
	// 0x34: 00110100 -> 00 / 11 / 010 / 0
		// 00: 0, 1, 2 중 0번째 카운터 선택
		// 11: 이후에 LSB 와 MSB 를 전송하겠다.
		// 010: 모드2(Rate Generator) 방식을 사용하겠다.
		// 0: 이진 카운팅 방식을 사용하겠다.
	outb(0x43, 0x34); /* CW: counter 0, LSB then MSB, mode 2, binary. */

	// outX 함수는 I/O port 에 8/16/32 bit 값을 보내는 연산
	// outb 함수는 byte(8bit) 단위로 보내는 함수이기 때문에, 16 bit 를 두 개로 나눠서 전송
	// https://wiki.osdev.org/Inline_Assembly/Examples

	// 0번째 timer 의 port 인 0x40에 LSB, MSB 를 보냄
	// 이 때 count 값(timer 주기) 은 16bit 이므로, 8bit 씩 나눠서 전송
	outb(0x40, count & 0xff);
	outb(0x40, count >> 8);

	// 결과적으로 1 초에 1193180 번 진동하는 timer 가 11932 번 진동할 때마다 신호를 보내므로, 10ms 마다 interrupt 가 발생함을 알 수 있다.

	// interrupt_hander[0x20] 에 timer_interrupt 함수를 mapping 한다.
	// 주소값이 0x20 인 이유는 다음과 같다.
		// 1. 8254 timer 의 0번 채널은 IRQ0 으로 interrupt 신호를 발생시킨다.
		// 2. IRQ0 번은 프로그래밍적으로 시스템 카운터로 정해져있다.
		// 3. 이 때 Interrupt 요청은, PIC(programmable Interrupt controller) 에 전달되는데, Pintos 에서 IRQ0 은 0x20 으로 변경되어 전달된다.
		// 4. 그러므로 intr_register_ext 함수에서 0x20 에 timer_interrupt 함수를 저장하는 것이다.
	// https://web.eecs.umich.edu/~ryanph/jhu/cs318/fall22/lectures/lec2_arch_annotated.pdf
	intr_register_ext(0x20, timer_interrupt, "8254 Timer");
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void timer_calibrate(void)
{
	unsigned high_bit, test_bit;

	ASSERT(intr_get_level() == INTR_ON);
	printf("Calibrating timer...  ");

	/* Approximate loops_per_tick as the largest power-of-two
	   still less than one timer tick. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops(loops_per_tick << 1))
	{
		loops_per_tick <<= 1;
		ASSERT(loops_per_tick != 0);
	}

	/* Refine the next 8 bits of loops_per_tick. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops(high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf("%'" PRIu64 " loops/s.\n", (uint64_t)loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks(void)
{
	enum intr_level old_level = intr_disable();
	int64_t t = ticks;
	intr_set_level(old_level);
	barrier();
	return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t
timer_elapsed(int64_t then)
{
	return timer_ticks() - then;
}

/* Suspends execution for approximately TICKS timer ticks. */
void timer_sleep(int64_t ticks)
{
	int64_t start = timer_ticks();

	ASSERT(intr_get_level() == INTR_ON);
	while (timer_elapsed(start) < ticks)
		thread_yield();
}

/* Suspends execution for approximately MS milliseconds. */
void timer_msleep(int64_t ms)
{
	real_time_sleep(ms, 1000);
}

/* Suspends execution for approximately US microseconds. */
void timer_usleep(int64_t us)
{
	real_time_sleep(us, 1000 * 1000);
}

/* Suspends execution for approximately NS nanoseconds. */
void timer_nsleep(int64_t ns)
{
	real_time_sleep(ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void timer_print_stats(void)
{
	printf("Timer: %" PRId64 " ticks\n", timer_ticks());
}

/* Timer interrupt handler. */
static void
timer_interrupt(struct intr_frame *args UNUSED)
{
	ticks++;
	thread_tick();
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops(unsigned loops)
{
	/* Wait for a timer tick. */
	int64_t start = ticks;
	while (ticks == start)
		barrier();

	/* Run LOOPS loops. */
	start = ticks;
	busy_wait(loops);

	/* If the tick count changed, we iterated too long. */
	barrier();
	return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait(int64_t loops)
{
	while (loops-- > 0)
		barrier();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep(int64_t num, int32_t denom)
{
	/* Convert NUM/DENOM seconds into timer ticks, rounding down.

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT(intr_get_level() == INTR_ON);
	if (ticks > 0)
	{
		/* We're waiting for at least one full timer tick.  Use
		   timer_sleep() because it will yield the CPU to other
		   processes. */
		timer_sleep(ticks);
	}
	else
	{
		/* Otherwise, use a busy-wait loop for more accurate
		   sub-tick timing.  We scale the numerator and denominator
		   down by 1000 to avoid the possibility of overflow. */
		ASSERT(denom % 1000 == 0);
		busy_wait(loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}
