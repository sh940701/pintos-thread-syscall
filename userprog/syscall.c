#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/palloc.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
void check_address(void *addr);
void exit(int status);
int exec(const char *file);
bool create(const char *file, unsigned iniital_size);
bool remove(const char *file);
int open(const char *filename);

/* 시스템 호출.
 *
 * 이전에 시스템 호출 서비스는 인터럽트 핸들러에서 처리되었습니다
 * (예: 리눅스에서 int 0x80). 그러나 x86-64에서는 제조업체가 시스템 호출을
 * 요청하기 위한 효율적인 경로를 제공합니다, 바로 `syscall` 명령어입니다.
 *
 * syscall 명령어는 모델별 레지스터(MSR)에서 값을 읽어와서 작동합니다.
 * 자세한 내용은 메뉴얼을 참조하세요. */

#define MSR_STAR 0xc0000081			/* 세그먼트 선택자 msr */
#define MSR_LSTAR 0xc0000082		/* Long mode SYSCALL 목적지 */
#define MSR_SYSCALL_MASK 0xc0000084 /* eflags를 위한 마스크 */

void syscall_init(void)
{
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
							((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* 인터럽트 서비스 루틴은 syscall_entry가 유저랜드 스택을 커널
	 * 모드 스택으로 교체하기 전까지 어떤 인터럽트도 처리해서는 안됩니다.
	 * 따라서 FLAG_FL을 마스킹했습니다. */
	write_msr(MSR_SYSCALL_MASK,
			  FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}
/* The main system call interface */
void syscall_handler(struct intr_frame *f)
{
	uint64_t sys_no = f->R.rax;
	if (sys_no >= 0x0 && sys_no <= 0x18)
	{
		switch (sys_no)
		{
			/* Halt the operating system. */
		case SYS_HALT:
			halt();
			break;
			/* Terminate this process. */
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
			/* Clone current process. */
		case SYS_FORK:
			break;
			/* Switch current process. */
		case SYS_EXEC:
			f->R.rax = exec(f->R.rdi);
			break;
			/* Wait for a child process to die. */
		case SYS_WAIT:
			break;
			/* Create a file. */
		case SYS_CREATE:
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
			/* Delete a file. */
		case SYS_REMOVE:
			f->R.rax = remove(f->R.rdi);
			break;
			/* Open a file. */
		case SYS_OPEN:
			// rax == SYS_OPEN
			// rdi == filename
			f->R.rax = open(f->R.rdi);
			break;
			/* Obtain a file's size. */
		case SYS_FILESIZE:
			break;
			/* Read from a file. */
		case SYS_READ:
			break;
			/* Write to a file. */
		case SYS_WRITE:
			// check_address(f->R.rsi);
			printf("%s", f->R.rsi);
			break;
			/* Change position in a file. */
		case SYS_SEEK:
			break;
			/* Report current position in a file. */
		case SYS_TELL:
			break;
			/* Close a file. */
		case SYS_CLOSE:
			break;
		}
	}
	else
	{
		printf("Whaaaatt?!?@?!@#!!!\n");
	}
	// thread_exit();
}
void check_address(void *addr)
{
	struct thread *t = thread_current();
	if (!is_user_vaddr(addr) || addr == NULL ||
		pml4_get_page(t->pml4, addr) == NULL)
	{
		exit(-1);
	}
}
void halt()
{
	power_off();
}
void exit(int status)
{
	struct thread *curr = thread_current();
	printf("%s: exit(%d)\n", curr->name, status);
	thread_exit();
}
int exec(const char *file)
{
	check_address(file);
	int len = strlen(file) + 1;
	char *file_name = palloc_get_page(PAL_ZERO);

	if (file_name == NULL)
	{
		exit(-1);
	}
	strlcpy(file_name, file, len);
	if (process_exec(file_name) == -1)
	{
		return -1;
	}
	NOT_REACHED();
	return 0;
}
bool create(const char *file, unsigned int iniital_size)
{
	check_address(file);
	return filesys_create(file, iniital_size);
}
bool remove(const char *file)
{
	check_address(file);
	return filesys_remove(file);
}
int open(const char *filename) {
	
}