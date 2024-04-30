#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif

/* States in a thread's life cycle. */
enum thread_status
{
	THREAD_RUNNING, /* Running thread. */
	THREAD_READY,	/* Not running but ready to run. */
	THREAD_BLOCKED, /* Waiting for an event to trigger. */
	THREAD_DYING	/* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t)-1) /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0	   /* Lowest priority. */
#define PRI_DEFAULT 31 /* Default priority. */
#define PRI_MAX 63	   /* Highest priority. */

/* 커널 스레드 또는 사용자 프로세스입니다.
 *
 * 각 스레드 구조는 자체 4 kB 페이지에 저장됩니다.
 * 스레드 구조 자체는 페이지의 맨 아래에 있습니다(offset 0).
 * 페이지의 나머지 부분은 스레드의 커널 스택을 위해 예약되어 있으며,
 * 이는 페이지의 맨 위부터 아래로 성장합니다(offset 4 kB).
 * 다음은 이를 설명한 그림입니다:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
이로 인한 결과는 두 가지로 나뉩니다:

첫째, 'struct thread'는 너무 커지지 않도록 허용되어야 합니다. 그렇지 않으면 커널 스택에 충분한 공간이 없을 수 있습니다. 우리의 기본 'struct thread'는 몇 바이트 크기입니다. 이것은 아마도 1KB 미만으로 유지되어야 합니다.
둘째, 커널 스택은 너무 커지지 않도록 허용되어야 합니다. 스택이 오버플로우되면 스레드 상태가 손상될 수 있습니다. 따라서 커널 함수는 비정적 지역 변수로 큰 구조체나 배열을 할당해서는 안 됩니다. 대신 malloc() 또는 palloc_get_page()를 사용하여 동적 할당을 해야 합니다.
이러한 문제 중 하나의 첫 번째 증상은 아마도 thread_current()에서 단언 오류일 것입니다. 이 함수는 실행 중인 스레드의 'struct thread'의 'magic' 멤버가 THREAD_MAGIC으로 설정되어 있는지 확인합니다. 스택 오버플로우는 일반적으로 이 값을 변경하여 단언을 트리거합니다. */

/* 'elem' 멤버는 두 가지 용도로 사용됩니다.

런 큐 (thread.c)의 요소이거나 세마포어 대기 목록 (synch.c)의 요소가 될 수 있습니다.
이 두 가지 방법만 사용할 수 있는 이유는 서로 배타적이기 때문입니다: 준비 상태의 스레드만이 런 큐에 있고, 차단된 상태의 스레드만이 세마포어 대기 목록에 있습니다.
*/
struct thread
{
	/* Owned by thread.c. */
	tid_t tid;				   /* Thread identifier. */
	enum thread_status status; /* Thread state. */
	char name[16];			   /* Name (for debugging purposes). */
	int priority;			   /* Priority. */
	int64_t wake_tick;		   // #1 Alarm-Clock : wake할 시간

	/* Shared between thread.c and synch.c. */
	struct list_elem elem; /* List element. */

	/* #2 Priority Scheduling : donation list와 pre_priority */
	int init_priority;
	struct lock *wait_on_lock;
	struct list donations;
	struct list_elem donation_elem;

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4; /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf; /* Information for switching */
	unsigned magic;		  /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init(void);
void thread_start(void);

void thread_tick(void);
void thread_print_stats(void);

typedef void thread_func(void *aux);
tid_t thread_create(const char *name, int priority, thread_func *, void *);

void thread_block(void);
void thread_unblock(struct thread *);

struct thread *thread_current(void);
tid_t thread_tid(void);
const char *thread_name(void);

void thread_exit(void) NO_RETURN;
void thread_yield(void);

/* #2 Priority Scheduling : list의 쓰레드의 우선순위 비교 */
bool compare_thread_priority(const struct list_elem *a,
							 const struct list_elem *b,
							 void *aux);
/* #2 Priority Scheduling : 현재 실행 중인 쓰레드보다 더 높은 우선순위의 쓰레드가 ready_list에 있다면 양보 */
void priority_schedule(void);
int thread_get_priority(void);
void thread_set_priority(int);

int thread_get_nice(void);
void thread_set_nice(int);
int thread_get_recent_cpu(void);
int thread_get_load_avg(void);

void do_iret(struct intr_frame *tf);

#endif /* threads/thread.h */
