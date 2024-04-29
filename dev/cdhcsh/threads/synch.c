/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* #2 Priority Scheduling : 우선순위 비교 함수*/
static bool compare_priority(const struct list_elem *a,
							 const struct list_elem *b,
							 void *aux)
{
	struct thread *a_t, *b_t;
	a_t = list_entry(a, struct thread, elem);
	b_t = list_entry(b, struct thread, elem);
	return a_t->priority > b_t->priority;
}

/* 세마포어 SEMA를 VALUE로 초기화합니다.
세마포어는 0 이상의 정수와 두 가지 원자적 연산을 가진다.

down 또는 "P": 값이 양수가 될 때까지 기다린 후 값을 감소시킵니다.
up 또는 "V": 값을 증가시킵니다(그리고 기다리는 스레드 중 하나를 깨웁니다).
*/
void sema_init(struct semaphore *sema, unsigned value)
{
	ASSERT(sema != NULL);

	sema->value = value;
	list_init(&sema->waiters);
}

/* 세마포어의 down 또는 "P" 연산입니다.
SEMA의 값을 양수가 될 때까지 기다린 다음 원자적으로 감소시킵니다.

이 함수는 잠금이 풀릴 때까지 기다리므로 인터럽트 핸들러 내에서 호출해서는 안됩니다.
이 함수는 인터럽트가 비활성화 된 상태에서 호출될 수 있지만, 만약 이 함수가
슬립을 유발한다면 다음 예정된 스레드는 아마도 다시 인터럽트를 켤 것입니다.
이것은 sema_down 함수입니다.
*/
void sema_down(struct semaphore *sema)
{
	enum intr_level old_level;

	ASSERT(sema != NULL);
	ASSERT(!intr_context());

	old_level = intr_disable();
	while (sema->value == 0)
	{
		list_insert_ordered(&sema->waiters, &thread_current()->elem, compare_priority, NULL);
		thread_block();
	}
	sema->value--;
	intr_set_level(old_level);
}

/* 세마포어의 down 또는 "P" 연산입니다.
세마포어가 이미 0이 아니면 true를 반환하고 그렇지 않으면 false를 반환합니다.

이 함수는 인터럽트 핸들러에서 호출될 수 있습니다.
*/
bool sema_try_down(struct semaphore *sema)
{
	enum intr_level old_level;
	bool success;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level(old_level);

	return success;
}

/* 세마포어의 up 또는 "V" 연산입니다.
SEMA의 값을 증가시키고 SEMA에 대기 중인 스레드 중 하나를 깨웁니다.

이 함수는 인터럽트 핸들러에서 호출될 수 있습니다.
*/
void sema_up(struct semaphore *sema)
{
	enum intr_level old_level;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (!list_empty(&sema->waiters))
	{
		list_sort(&sema->waiters, compare_priority, NULL);
		thread_unblock(list_entry(list_pop_front(&sema->waiters),
								  struct thread, elem));
	}
	sema->value++;
	intr_set_level(old_level);
	priority_schedule();
}

static void sema_test_helper(void *sema_);

/* 세마포어 쌍 간에 "핑퐁"하는 자체 테스트입니다.
무엇이 일어나고 있는지 보려면 printf() 호출을 삽입하세요.
*/
void sema_self_test(void)
{
	struct semaphore sema[2];
	int i;

	printf("Testing semaphores...");
	sema_init(&sema[0], 0);
	sema_init(&sema[1], 0);
	thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up(&sema[0]);
		sema_down(&sema[1]);
	}
	printf("done.\n");
}

/* sema_self_test()에서 사용하는 스레드 함수입니다. */
static void
sema_test_helper(void *sema_)
{
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down(&sema[0]);
		sema_up(&sema[1]);
	}
}

/* LOCK을 초기화합니다. 한 번에 최대 하나의 스레드가 LOCK을 보유할 수 있습니다.
   현재 스레드가 이미 LOCK을 보유하고 있어도 해당 LOCK을 획득하려고 시도하면 "재귀적"이지 않은 것이므로
   해당 LOCK을 획득하는 것은 오류이기 때문에 오류가 발생합니다.

   LOCK은 초기 값이 1인 세마포어의 특수화입니다.
   LOCK과 이러한 세마포어의 차이점은 두 가지입니다. 첫째, 세마포어는 1보다 큰 값을 가질 수 있지만
   LOCK은 한 번에 하나의 스레드만 소유할 수 있습니다. 둘째, 세마포어는 소유자가 없지만
   LOCK은 동일한 스레드가 획득하고 해제해야 합니다. 이러한 제약이 불편하게 느껴질 때
   LOCK 대신 세마포어를 사용하는 것이 좋습니다. */
void lock_init(struct lock *lock)
{
	ASSERT(lock != NULL);

	lock->holder = NULL;
	sema_init(&lock->semaphore, 1);
}

/* LOCK을 획득합니다. 필요한 경우 가능할 때까지 대기하여 사용 가능해집니다.
   현재 스레드가 이미 LOCK을 보유하고 있으면 안 됩니다.

   이 함수는 대기할 수 있으므로 인터럽트 핸들러 내에서 호출해서는 안 됩니다.
   이 함수는 인터럽트가 비활성화된 상태에서 호출될 수 있지만,
   대기가 필요하면 인터럽트를 다시 활성화해야 합니다. */
void lock_acquire(struct lock *lock)
{
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(!lock_held_by_current_thread(lock));

	sema_down(&lock->semaphore);
	lock->holder = thread_current();
}

/* LOCK을 시도하고 성공하면 true를 반환하고 실패하면 false를 반환합니다.
   현재 스레드가 이미 LOCK을 보유하고 있으면 안 됩니다.

   이 함수는 대기하지 않으므로 인터럽트 핸들러 내에서 호출할 수 있습니다. */
bool lock_try_acquire(struct lock *lock)
{
	bool success;

	ASSERT(lock != NULL);
	ASSERT(!lock_held_by_current_thread(lock));

	success = sema_try_down(&lock->semaphore);
	if (success)
		lock->holder = thread_current();
	return success;
}

/* 현재 스레드가 소유한 LOCK을 해제합니다.
   이것은 lock_release 함수입니다.

   인터럽트 핸들러는 잠금을 획득할 수 없으므로
   인터럽트 핸들러 내에서 잠금을 해제하는 것은 의미가 없습니다. */
void lock_release(struct lock *lock)
{
	ASSERT(lock != NULL);
	ASSERT(lock_held_by_current_thread(lock));

	lock->holder = NULL;
	sema_up(&lock->semaphore);
}

/* 현재 스레드가 LOCK을 소유하고 있는지 여부를 반환합니다.
   만약 현재 스레드가 LOCK을 소유하고 있다면 true를 반환하고 그렇지 않으면 false를 반환합니다.
   (다른 스레드가 잠금을 보유하고 있는지 확인하는 것은 경쟁 조건에 노출될 수 있습니다.) */
bool lock_held_by_current_thread(const struct lock *lock)
{
	ASSERT(lock != NULL);

	return lock->holder == thread_current();
}

/* One semaphore in a list. */
struct semaphore_elem
{
	struct list_elem elem;		/* List element. */
	struct semaphore semaphore; /* This semaphore. */
};

/* 조건 변수 COND를 초기화합니다.
   조건 변수는 코드 한 곳에서 조건을 신호로 보내고 협력 코드에서 해당 신호를 받아들여 동작을 취할 수 있도록 합니다. */
void cond_init(struct condition *cond)
{
	ASSERT(cond != NULL);

	list_init(&cond->waiters);
}

/* LOCK을 원자적으로 해제하고 다른 코드에서 조건을 신호로 받을 때까지 대기합니다.
   COND가 신호를 받으면 LOCK을 다시 획득합니다. 이 함수를 호출하기 전에 LOCK을 보유해야 합니다.

   이 함수에 의해 구현된 모니터는 "Mesa" 스타일이며 "Hoare" 스타일이 아닙니다.
   즉, 신호를 보내거나 받는 것은 원자적인 작업이 아닙니다. 따라서 보통은 대기가 완료된 후 조건을 다시 확인하고,
   필요한 경우 다시 대기해야 합니다.

   특정 조건 변수는 단 하나의 잠금에만 연결되어 있지만 하나의 잠금은 여러 조건 변수에 연결될 수 있습니다.
   즉, 잠금에서 조건 변수로의 일대다 매핑이 있습니다.

   이 함수는 대기할 수 있으므로 인터럽트 핸들러 내에서 호출해서는 안 됩니다.
   이 함수는 인터럽트가 비활성화된 상태에서 호출될 수 있지만,
   대기가 필요하면 인터럽트를 다시 활성화해야 합니다. */
void cond_wait(struct condition *cond, struct lock *lock)
{
	struct semaphore_elem waiter;

	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	sema_init(&waiter.semaphore, 0);
	list_insert_ordered(&cond->waiters, &waiter.elem, compare_priority, NULL);
	lock_release(lock);
	sema_down(&waiter.semaphore);
	lock_acquire(lock);
}

/* LOCK(LOCK이 보호하는)에 의해 보호된 COND에서 대기 중인 스레드가 있다면
   그 중 하나에게 신호를 보내 깨웁니다. 이 함수를 호출하기 전에 LOCK을 보유해야 합니다.

   인터럽트 핸들러는 잠금을 획득할 수 없으므로
   인터럽트 핸들러 내에서 조건 변수를 신호로 보내는 것은 의미가 없습니다. */
void cond_signal(struct condition *cond, struct lock *lock UNUSED)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	if (!list_empty(&cond->waiters))
	{
		list_sort(&cond->waiters, compare_priority, NULL);
		sema_up(&list_entry(list_pop_front(&cond->waiters),
							struct semaphore_elem, elem)
					 ->semaphore);
	}
}

/* 대기 중인 스레드(있는 경우) 모두를 깨웁니다.
   대기 중인 스레드가 있을 때까지 반복하여 조건을 신호로 보냅니다.
   이 함수를 호출하기 전에 LOCK을 보유해야 합니다.

   인터럽트 핸들러는 잠금을 획득할 수 없으므로
   인터럽트 핸들러 내에서 조건 변수를 신호로 보내는 것은 의미가 없습니다. */
void cond_broadcast(struct condition *cond, struct lock *lock)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);

	while (!list_empty(&cond->waiters))
		cond_signal(cond, lock);
}
