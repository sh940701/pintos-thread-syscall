#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#include "synch.h"
#endif

/* struct thread의 `magic' 멤버에 대한 무작위 값.
   스택 오버플로우를 감지하기 위해 사용됨.  자세한 내용은
   thread.h 파일 맨 위의 큰 주석 참조. */
#define THREAD_MAGIC 0xcd6abf4b

/* 기본 쓰레드용 무작위 값
   이 값을 수정하지 마십시오. */
#define THREAD_BASIC 0xd42df210


/* THREAD_READY 상태의 프로세스 목록. 즉, 실행 준비가 된 프로세스
   실제로 실행 중이지는 않음. */
static struct list ready_list;

/* Idle thread. */
static struct thread *idle_thread;

/* 초기 쓰레드, init.c:main()에서 실행 중인 쓰레드. */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* 통계. */
static long long idle_ticks;   /* 유휴 시간에 소비된 타이머 틱 수. */
static long long kernel_ticks; /* 커널 쓰레드에서 소비된 타이머 틱 수. */
static long long user_ticks;   /* 사용자 프로그램에서 소비된 타이머 틱 수. */

/* 스케줄링. */
#define TIME_SLICE 4		  /* 각 쓰레드에 할당할 타이머 틱 수. */
static unsigned thread_ticks; /* 마지막 양보 이후의 타이머 틱 수. */

/* false(기본값)인 경우 라운드 로빈 스케줄러를 사용합니다.
   true인 경우 다단계 피드백 큐 스케줄러를 사용합니다.
   커널 명령줄 옵션 "-o mlfqs"에 의해 제어됩니다. */
bool thread_mlfqs;

static void kernel_thread(thread_func *, void *aux);

static void idle(void *aux UNUSED);
static struct thread *next_thread_to_run(void);
static void init_thread(struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule(void);
static tid_t allocate_tid(void);

/* T가 유효한 쓰레드를 가리키는지 여부를 반환합니다. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* 실행 중인 쓰레드를 반환합니다.
 * CPU의 스택 포인터 `rsp`를 읽은 다음
 * 페이지의 시작 부분으로 내립니다. `struct thread`가
 * 항상 페이지의 시작 부분에 있고 스택 포인터는
 * 중간에 있기 때문에, 이는 현재 쓰레드를 찾습니다. */
#define running_thread() ((struct thread *)(pg_round_down(rrsp())))

// 쓰레드 시작을 위한 전역 설명자 테이블.
// gdt는 thread_init 이후에 설정될 것이므로,
// 먼저 임시 gdt를 설정해야 합니다.
static uint64_t gdt[3] = {0, 0x00af9a000000ffff, 0x00cf92000000ffff};

/* 쓰레딩 시스템을 초기화합니다.
   현재 실행 중인 코드를 쓰레드로 변환하여 초기화합니다.
   이는 일반적으로 작동하지 않으며, 여기서만 가능합니다.
   loader.S가 스택의 맨 아래를 페이지 경계로 놓도록 주의했기 때문입니다.

   또한 실행 대기열과 tid 락을 초기화합니다.

   이 함수를 호출한 후에는
   thread_current()를 호출하기 전에
   페이지 할당기를 초기화해야합니다.

   이 함수가 완료될 때까지 thread_current()를 호출하는 것은 안전하지 않습니다. */
void thread_init(void)
{
	ASSERT(intr_get_level() == INTR_OFF);

	/* 커널을 위한 임시 gdt 다시 로드
	 * 이 gdt에는 사용자 컨텍스트가 포함되지 않습니다.
	 * 커널은 gdt_init()에서 사용자 컨텍스트와 함께 gdt를 재구축할 것입니다. */
	struct desc_ptr gdt_ds = {
		.size = sizeof(gdt) - 1,
		.address = (uint64_t)gdt};
	lgdt(&gdt_ds);

	/* 전역 쓰레드 컨텍스트 초기화 */
	lock_init(&tid_lock);
	list_init(&ready_list);
	list_init(&destruction_req);

	/* 실행 중인 쓰레드에 대한 스레드 구조체 설정 */
	initial_thread = running_thread();
	init_thread(initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid();
}

/* 인터럽트를 사용하여 선점형 쓰레드 스케줄링을 시작합니다.
   또한 idle 쓰레드를 생성합니다. */
void thread_start(void)
{
	/* idle 쓰레드 생성 */
	struct semaphore idle_started;	
	sema_init(&idle_started, 0);
	thread_create("idle", PRI_MIN, idle, &idle_started);
	/* 선점형 쓰레드 스케줄링 시작 */
	intr_enable();
	/* idle 쓰레드가 idle_thread를 초기화할 때까지 기다립니다. */
	sema_down(&idle_started);
}

/* 타이머 인터럽트 핸들러에 의해 각 타이머 틱마다 호출됩니다.
   따라서 이 함수는 외부 인터럽트 컨텍스트에서 실행됩니다. */
void thread_tick(void)
{
	struct thread *t = thread_current();

	/* 통계 업데이트 */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* 선점 적용 */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return();
}

/* 쓰레드 통계를 출력합니다. */
void thread_print_stats(void)
{
	printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
		   idle_ticks, kernel_ticks, user_ticks);
}

/* 이름이 NAME이고 초기 우선순위가 주어진 새 커널 쓰레드를 생성하고
   AUX를 인수로 전달하여 FUNCTION을 실행하고 준비 큐에 추가합니다.
   새 쓰레드에 대한 쓰레드 식별자를 반환하거나
   생성에 실패하면 TID_ERROR를 반환합니다.

   thread_start()가 호출되었다면, 새 쓰레드가 반환되기 전에
   thread_create()가 반환되기 전에 예약될 수 있습니다. 그것은 심지어 종료
   되기 전에 thread_create()를 반환 할 수도 있습니다.
   순서를 보장해야 하는 경우 세마포어나 다른 형태의 동기화를 사용하십시오.

   제공된 코드는 새 쓰레드의 `priority' 멤버를
   PRIORITY로 설정하지만, 실제로는 우선순위 스케줄링이 구현되어 있지 않습니다.
   우선순위 스케줄링은 문제 1-3의 목표입니다. */
tid_t thread_create(const char *name, int priority,
					thread_func *function, void *aux)
{
	struct thread *t, *t2;
	tid_t tid;
	ASSERT(function != NULL);

	/* 쓰레드 할당 */
	t = palloc_get_page(PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread(t, name, priority);
	tid = t->tid = allocate_tid();
	/* 스케줄되면 kernel_thread 호출
	 * 참고) rdi는 1번째 인수이고 rsi는 2번째 인수입니다. */
	t->tf.rip = (uintptr_t)kernel_thread;
	t->tf.R.rdi = (uint64_t)function;
	t->tf.R.rsi = (uint64_t)aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;
	/* 실행 대기열에 추가 */
	thread_unblock(t);
	test_max_priority();

	return tid;
}

/* 현재 쓰레드를 슬립 상태로 만듭니다.
   thread_unblock()에 의해 깨어날 때까지 다시 스케줄되지 않습니다.

   이 함수는 인터럽트가 비활성화된 상태에서 호출되어야 합니다.
   일반적으로 synch.h의 동기화 기본 형식을 사용하는 것이 더 좋습니다. */
void thread_block(void)
{
	ASSERT(!intr_context()); // 외부 인터럽트가 실행중이지 않아야 함. 안 그러면 현재 스레드가 외부 스레드(메인?)이기 때문
	ASSERT(intr_get_level() == INTR_OFF); // 인터럽트가 꺼져 있어야 함. 스레드 블록은 다음 스레드를 불러오는 과정이 반드시 따라오기 때문
	thread_current()->status = THREAD_BLOCKED;
	schedule();
}

/* 블록된 쓰레드 T를 실행 준비 상태로 전환합니다.
   T가 블록되지 않았으면 이것은 에러입니다. (호출자가 직접
   쓰레드를 준비 상태로 만들려면 thread_yield()를 사용하십시오.)

   이 함수는 실행 중인 쓰레드를 선점하지 않습니다. 이것은 중요합니다.
   호출자가 인터럽트를 직접 비활성화하고 있으면
   쓰레드를 원자적으로 블록 해제하고
   다른 데이터를 업데이트할 수 있다고 예상할 수 있습니다. */
void thread_unblock(struct thread *t)
{
	enum intr_level old_level;

	ASSERT(is_thread(t));

	old_level = intr_disable();
	ASSERT(t->status == THREAD_BLOCKED);
	// list_push_back(&ready_list, &t->elem);
	list_insert_ordered(&ready_list, &t->elem, compare_elem, NULL);
	t->status = THREAD_READY;
	intr_set_level(old_level);
}

// a : 새로운 스레드, b : 현재 스레드
bool compare_elem(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED){

	struct thread *t1 = list_entry(a, struct thread, elem);
	struct thread *t2 = list_entry(b, struct thread, elem);
	// 새로운 스레드가 더 높은 우선순위 = 0, 아니면 1
	if (t1->priority > t2->priority){
		return 1;
	}else{
		return 0;
	}
}

/* 실행 중인 쓰레드의 이름을 반환합니다. */
const char *
thread_name(void)
{
	return thread_current()->name;
}

/* 실행 중인 쓰레드를 반환합니다.
   이것은 running_thread()에 몇 가지 안전 검사를 추가한 것입니다.
   자세한 내용은 thread.h 맨 위의 큰 주석을 참조하십시오. */
struct thread *
thread_current(void)
{
	struct thread *t = running_thread();

	/* T가 실제로 쓰레드인지 확인합니다.
   이러한 단언 중 하나라도 작동하지 않으면
   쓰레드가 스택을 오버플로우한 것입니다.
   각 쓰레드의 스택은 4 KB 미만이므로
   큰 자동 배열이나 중간 정도의 재귀로 인해
   스택 오버플로우가 발생할 수 있습니다. */
	ASSERT(is_thread(t));
	ASSERT(t->status == THREAD_RUNNING);

	return t;
}

/* 실행 중인 쓰레드의 TID를 반환합니다. */
tid_t thread_tid(void)
{
	return thread_current()->tid;
}

/* 현재 쓰레드를 종료합니다.
   이 함수는 실행 중인 쓰레드에 대해서만 호출됩니다.
   실행 중인 쓰레드는 종료됩니다. 쓰레드가 반환되면
   프로세스를 종료합니다. */
void thread_exit(void)
{
	ASSERT(!intr_context());

#ifdef USERPROG
	process_exit();
#endif

	/* 상태를 죽어가는 중으로 설정하고 다른 프로세스를 예약하면 됩니다.
	   schedule_tail()을 호출하는 동안 소멸됩니다. */
	intr_disable();
	do_schedule(THREAD_DYING);
	NOT_REACHED();
}

/* CPU를 반환합니다.  현재 스레드는 절전 모드로 전환되지 않으며 스케줄러의 임의로 즉시 다시 스케줄링할 수 있습니다. */
void thread_yield(void)
{
	struct thread *curr = thread_current();
	enum intr_level old_level;

	ASSERT(!intr_context());

	old_level = intr_disable();
	if (curr != idle_thread)
		list_insert_ordered(&ready_list, &curr->elem, compare_elem, NULL);
	do_schedule(THREAD_READY);
	intr_set_level(old_level);
}

/* 현재 쓰레드의 우선 순위를 NEW_PRIORITY로 설정합니다. */
void thread_set_priority(int new_priority)
{
	thread_current()->priority = new_priority;
	thread_current()->init_priority = new_priority;
	refresh_priority();
	test_max_priority();

}

void test_max_priority(void){
	if (!list_empty (&ready_list) && 
    thread_current()->priority < 
    list_entry(list_front (&ready_list), struct thread, elem)->priority)
        thread_yield ();
}

/* 현재 쓰레드의 우선 순위를 반환합니다. */
int thread_get_priority(void)
{
	return thread_current()->priority;
}

/* 현재 쓰레드의 nice 값을 NICE로 설정합니다. */
void thread_set_nice(int nice UNUSED)
{
	/* TODO: Your implementation goes here */
}

/* 현재 쓰레드의 nice 값을 반환합니다. */
int thread_get_nice(void)
{
	/* TODO: Your implementation goes here */
	return 0;
}

/* 시스템 로드 평균의 100배를 반환합니다. */
int thread_get_load_avg(void)
{
	/* TODO: Your implementation goes here */
	return 0;
}

/* 현재 쓰레드의 recent_cpu 값의 100배를 반환합니다. */
int thread_get_recent_cpu(void)
{
	/* TODO: Your implementation goes here */
	return 0;
}

/* 아이들 쓰레드. 다른 쓰레드가 준비되지 않은 경우 실행됩니다.

   아이들 쓰레드는 처음에 준비 목록에 넣고
   thread_start()에서 호출됩니다. 초기에 한 번만 예약됩니다.
   그런 다음 아이들 쓰레드는 초기화되고, 넘겨진 세마포어를 초기화합니다.
   그러고 나서 바로 차단됩니다.
   이후 아이들 쓰레드는 준비 목록에 나타나지 않습니다. 준비 목록이 비어 있는 경우
   특수 케이스로 next_thread_to_run()에서 반환됩니다. */
static void
idle(void *idle_started_ UNUSED)
{
	struct semaphore *idle_started = idle_started_;
	idle_thread = thread_current();
	sema_up(idle_started);
	for (;;)
	{
		/* 누군가가 실행할 수 있도록 합니다. */
		intr_disable();
		thread_block();
		/* 인터럽트를 다시 활성화하고 다음 인터럽트를 기다립니다.

				   'sti' 명령은 다음 명령이 완료될 때까지 인터럽트를 비활성화합니다.
				   따라서 이 두 명령은 원자적으로 실행됩니다. 이러한 원자성은
				   중요합니다. 그렇지 않으면 인터럽트가 처리될 수 있습니다.
				   인터럽트를 다시 활성화하고 다음 인터럽트를 기다리는 동안
				   시간당 최대 한 클럭 틱의 시간이 소비됩니다.

				   [IA32-v2a] "HLT", [IA32-v2b] "STI" 및 [IA32-v3a]를 참조하십시오.
				   7.11.1 "HLT Instruction". */
		asm volatile("sti; hlt" : : : "memory");
	}
}

/* 커널 쓰레드의 기초가 되는 함수. */
static void
kernel_thread(thread_func *function, void *aux)
{
	ASSERT(function != NULL);

	intr_enable(); /* 스케줄러는 인터럽트가 꺼져 있을 때 실행됩니다. */
	function(aux); /* 쓰레드 함수 실행 */
	thread_exit(); /* function()이 반환되면 쓰레드를 종료합니다. */
}

/* T를 NAME으로 이름 지어 PRI의 초기 값으로 초기화합니다. */
static void
init_thread(struct thread *t, const char *name, int priority)
{
	ASSERT(t != NULL);
	ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT(name != NULL);

	memset(t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy(t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t)t + PGSIZE - sizeof(void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;
	t->init_priority = priority;
	t->wait_on_lock = NULL;
	list_init(&t->donor);
}

/* 스케줄할 다음 쓰레드를 선택하고 반환합니다.
   실행 대기열에서 쓰레드를 반환해야 합니다. 실행 대기열이
   비어있으면 idle_thread를 반환합니다. */
static struct thread *
next_thread_to_run(void)
{
	if (list_empty(&ready_list))
		return idle_thread;
	else
		return list_entry(list_pop_front(&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void do_iret(struct intr_frame *tf)
{
	__asm __volatile(
		"movq %0, %%rsp\n"
		"movq 0(%%rsp),%%r15\n"
		"movq 8(%%rsp),%%r14\n"
		"movq 16(%%rsp),%%r13\n"
		"movq 24(%%rsp),%%r12\n"
		"movq 32(%%rsp),%%r11\n"
		"movq 40(%%rsp),%%r10\n"
		"movq 48(%%rsp),%%r9\n"
		"movq 56(%%rsp),%%r8\n"
		"movq 64(%%rsp),%%rsi\n"
		"movq 72(%%rsp),%%rdi\n"
		"movq 80(%%rsp),%%rbp\n"
		"movq 88(%%rsp),%%rdx\n"
		"movq 96(%%rsp),%%rcx\n"
		"movq 104(%%rsp),%%rbx\n"
		"movq 112(%%rsp),%%rax\n"
		"addq $120,%%rsp\n"
		"movw 8(%%rsp),%%ds\n"
		"movw (%%rsp),%%es\n"
		"addq $32, %%rsp\n"
		"iretq"
		: : "g"((uint64_t)tf) : "memory");
}

/* 새 스레드의 페이지
   테이블을 활성화하고, 이전 스레드가 죽어가는 경우 소멸시킵니다.

   이 함수를 호출할 때 방금 스레드에서 전환했습니다.
   PREV에서 새 스레드가 이미 실행 중이며 인터럽트는
   여전히 비활성화되어 있습니다.

   따라서 스레드 전환이
   완료될 때까지는 호출하는 것이 안전하지 않습니다.  실제로는 함수 끝에 printf()를
   를 함수 끝에 추가해야 합니다.  */
static void
thread_launch(struct thread *th)
{
	uint64_t tf_cur = (uint64_t)&running_thread()->tf;
	uint64_t tf = (uint64_t)&th->tf;
	ASSERT(intr_get_level() == INTR_OFF);

	/*주요 스위칭 로직입니다.
	 * 먼저 전체 실행 컨텍스트를 intr_frame에 복원하고
	 * do_iret을 호출하여 다음 스레드로 전환합니다.
	 * 여기서부터 스택을 사용해서는 안 됩니다.
	 * 전환이 완료될 때까지 스택을 사용해서는 안 됩니다.*/
	__asm __volatile(
		/* Store registers that will be used. */
		"push %%rax\n"
		"push %%rbx\n"
		"push %%rcx\n"
		/* Fetch input once */
		"movq %0, %%rax\n"
		"movq %1, %%rcx\n"
		"movq %%r15, 0(%%rax)\n"
		"movq %%r14, 8(%%rax)\n"
		"movq %%r13, 16(%%rax)\n"
		"movq %%r12, 24(%%rax)\n"
		"movq %%r11, 32(%%rax)\n"
		"movq %%r10, 40(%%rax)\n"
		"movq %%r9, 48(%%rax)\n"
		"movq %%r8, 56(%%rax)\n"
		"movq %%rsi, 64(%%rax)\n"
		"movq %%rdi, 72(%%rax)\n"
		"movq %%rbp, 80(%%rax)\n"
		"movq %%rdx, 88(%%rax)\n"
		"pop %%rbx\n" // Saved rcx
		"movq %%rbx, 96(%%rax)\n"
		"pop %%rbx\n" // Saved rbx
		"movq %%rbx, 104(%%rax)\n"
		"pop %%rbx\n" // Saved rax
		"movq %%rbx, 112(%%rax)\n"
		"addq $120, %%rax\n"
		"movw %%es, (%%rax)\n"
		"movw %%ds, 8(%%rax)\n"
		"addq $32, %%rax\n"	
		"call __next\n" // read the current rip.
		"__next:\n"
		"pop %%rbx\n"
		"addq $(out_iret -  __next), %%rbx\n"
		"movq %%rbx, 0(%%rax)\n" // rip
		"movw %%cs, 8(%%rax)\n"	 // cs
		"pushfq\n"
		"popq %%rbx\n"
		"mov %%rbx, 16(%%rax)\n" // eflags
		"mov %%rsp, 24(%%rax)\n" // rsp
		"movw %%ss, 32(%%rax)\n"
		"mov %%rcx, %%rdi\n"
		"call do_iret\n"
		"out_iret:\n"
		: : "g"(tf_cur), "g"(tf) : "memory");
}

/* 새 프로세스를 스케줄합니다. 진입시, 인터럽트가 비활성화되어야 합니다.
 * 이 함수는 현재 쓰레드의 상태를 status로 변경한 다음 다른 쓰레드를 찾아
 * 그것으로 전환합니다.
 * schedule()에서 printf()를 호출하는 것은 안전하지 않습니다. */
static void
do_schedule(int status)
{
	ASSERT(intr_get_level() == INTR_OFF);
	ASSERT(thread_current()->status == THREAD_RUNNING);
	while (!list_empty(&destruction_req))
	{
		struct thread *victim =
			list_entry(list_pop_front(&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current()->status = status;
	schedule();
}

static void
schedule(void)
{
	struct thread *curr = running_thread();
	struct thread *next = next_thread_to_run();

	ASSERT(intr_get_level() == INTR_OFF);
	ASSERT(curr->status != THREAD_RUNNING);
	ASSERT(is_thread(next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate(next);
#endif

	if (curr != next)
	{
		/* 전환한 스레드가 죽어가고 있으면 해당 스레드의 구조체
		   스레드를 파괴합니다. 이 작업은 늦게 이루어져야 thread_exit()가
		   스스로 깔개를 꺼내지 않도록 늦게 발생해야 합니다.
		   여기서는 페이지가 현재 스택에서 사용 중이므로
		   페이지가 현재 스택에서 사용되고 있기 때문입니다.
		   실제 소멸 로직은 스택의 시작 부분에서
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread)
		{
			ASSERT(curr != next);
			list_push_back(&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch(next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid(void)
{
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire(&tid_lock);
	tid = next_tid++;
	lock_release(&tid_lock);

	return tid;
}