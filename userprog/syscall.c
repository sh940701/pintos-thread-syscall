#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/palloc.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "threads/synch.h"
#include "userprog/syscall.h"
#include "threads/malloc.h"
#include "intrinsic.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);

/* User Memory */
void check_address(void *addr);

/* System Calls */
void halt();
void exit(int status);
int exec(const char *file);
bool create(const char *file, unsigned iniital_size);
bool remove(const char *file);
tid_t fork(const char *thread_name, struct intr_frame *f);
int wait(tid_t tid);
int open(const char *filename);
void close(int fd);
int read(int fd, void *buffer, unsigned size);
int filesize(int fd);
int write(int fd, void *buffer, unsigned size);
unsigned tell(int fd);
void seek(int fd, unsigned position);
int dup2(int oldfd, int newfd);

/* filesys lock */
struct lock fd_lock;

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
	lock_init(&fd_lock);

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
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		case SYS_FORK:
			f->R.rax = fork(f->R.rdi, f);
			break;
		case SYS_EXEC:
			f->R.rax = exec(f->R.rdi);
			break;
		case SYS_WAIT:
			f->R.rax = wait(f->R.rdi);
			break;
		case SYS_CREATE:
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			f->R.rax = remove(f->R.rdi);
			break;
		case SYS_OPEN:
			f->R.rax = open(f->R.rdi);
			break;
		case SYS_FILESIZE:
			f->R.rax = filesize(f->R.rdi);
			break;
		case SYS_READ:
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			seek(f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL:
			f->R.rax = tell(f->R.rdi);
			break;
		case SYS_CLOSE:
			close(f->R.rdi);
			break;
		case SYS_DUP2:
			f->R.rax = dup2(f->R.rdi, f->R.rsi);
			break;
		}
	}
}

/* [User Memory] check_address:
   addr가 user mode에서 접근 가능한 주소인지 확인 */
void check_address(void *addr)
{
	struct thread *t = thread_current();
	if (!is_user_vaddr(addr) || addr == NULL ||
		pml4_get_page(t->pml4, addr) == NULL)
	{
		exit(-1);
	}
}

/* [System call] halt:
 * 운영체제 종료 */
void halt()
{
	power_off();
}

/* [System call] exit:
 * 현재 프로세스의 종료상태를 status로 변경하고 프로세스 종료 */
void exit(int status)
{
	thread_current()->exit_status = status;
	printf("%s: exit(%d)\n", thread_current()->name, thread_current()->exit_status);
	thread_exit();
}

/* [System call] exec:
 * file_name 파일을 실행한다. */
int exec(const char *file_name)
{
	check_address(file_name);
	int len = strlen(file_name) + 1;
	char *fn_copy = palloc_get_page(PAL_ZERO);

	if (fn_copy == NULL)
	{
		exit(-1);
	}
	strlcpy(fn_copy, file_name, len);
	if (process_exec(fn_copy) == -1)
	{
		return -1;
	}
	NOT_REACHED();
	return 0;
}

/* [System call] create:
 * initial_size의 file_name의 이름을 가지는 파일 생성 */
bool create(const char *file_name, unsigned int iniital_size)
{
	check_address(file_name);
	return filesys_create(file_name, iniital_size);
}

/* [System call] wait:
 * file_name의 이름을 가진 파일 삭제 */
bool remove(const char *file_name)
{
	check_address(file_name);
	return filesys_remove(file_name);
}

/* [System call] fork:
   thread_name의 이름을 가지는 자식 프로세스 생성
   그 후 자식 프로세스의 pid 반환
   자식 프로세스는 0을 반환함 */
tid_t fork(const char *thread_name, struct intr_frame *f)
{
	check_address(thread_name);
	return process_fork(thread_name, f);
}

/* [System call] wait:
 * pid를 가진 자식 프로세스의 종료를 기다린다.
 * 자식 프로세스의 종료 상태 값을 반환 */
int wait(tid_t pid)
{
	return process_wait(pid);
}

/* [System call] open:
 * file_name의 파일을 연후 파일 디스크립터를 반환 */
int open(const char *file_name)
{
	check_address(file_name);

	struct thread *curr = thread_current();

	struct file *_file = NULL;
	struct file_elem *file_elem = NULL;
	struct fd_elem *fd_elem = NULL;

	if (list_size(&curr->fd_pool) > FD_MAX)
		return -1;

	_file = filesys_open(file_name);
	file_elem = new_file_elem();

	if (!_file || !file_elem)
		goto error;

	while (fd_find(curr->nextfd))
		curr->nextfd++;

	/* fd를 file_elem에 연결*/
	if (!(fd_elem = register_fd(file_elem, curr->nextfd)))
		goto error;

	curr->nextfd++;

	file_elem->file = _file;
	list_push_back(&curr->fd_pool, &file_elem->elem);

	return fd_elem->fd;

error:
	if (_file)
		file_close(_file);
	if (file_elem)
		free(file_elem);
	if (fd_elem)
		free(fd_elem);
	return -1;
}

/* [System call] close:
 * fd의 파일을 닫음 */
void close(int fd)
{
	/* fd에 할당된 fd_elem 삭제 */
	fd_close(fd_find(fd));
}

/* [System call] read:
 * fd의 파일에서 size만큼 읽어서 buffer에 저장한 후 길이 반환 */
int read(int fd, void *buffer, unsigned size)
{
	check_address(buffer);

	struct file *file = fd_get_file(fd);

	/* stdout 경우 종료, stdin 경우 input_getc 반환 */
	if (!file || file == FD_STDOUT)
		return -1;
	if (file == FD_STDIN)
		return input_getc();

	/* 동기화를 위한 lock 사용 */
	lock_acquire(&fd_lock);
	int cnt = file_read(file, buffer, size);
	lock_release(&fd_lock);

	return cnt;
}

/* [System call] filesize:
 * fd의 파일의 filesize 반환*/
int filesize(int fd)
{
	struct file *file = fd_get_file(fd);

	/* stdin,stdout이 아니라면 file_length 반환 */
	if (file && is_file(file))
		return file_length(file);
	else
		return -1;
}

/* [System call] write:
 * fd의 파일에 buffer의 값을 size만큼 쓴 후 길이 반환 */
int write(int fd, void *buffer, unsigned size)
{
	check_address(buffer);

	struct file *file = fd_get_file(fd);

	/* stdin일 경우 종료, stdout일 경우 putbuf*/
	if (!file || file == FD_STDIN)
		return -1;
	if (file == FD_STDOUT)
	{
		putbuf(buffer, size);
		return size;
	}

	/* 동기화를 위한 lock 사용 */
	lock_acquire(&fd_lock);
	int cnt = file_write(file, buffer, size);
	lock_release(&fd_lock);

	return cnt;
}

/* [System call] tell:
 * fd의 파일의 pos 반환 (커서 위치 반환)*/
unsigned tell(int fd)
{
	struct file *file = fd_get_file(fd);

	/* stdin,stdout이 아니라면 file_tell 반환 */
	if (file && is_file(file))
		return file_tell(file);
	else
		return -1;
}

/* [System call] seek:
 * fd의 파일의 pos 변경 (커서 변경) */
void seek(int fd, unsigned position)
{
	if (position < 0)
		return;

	struct file *file = fd_get_file(fd);

	/* stdin,stdout이 아니라면 file_seek */
	if (file && is_file(file))
		file_seek(file, position);
}

/* [System call] dub2(extra):
 * oldfd 파일 디스크립터를 newfd에 복제하여 등록 */
int dup2(int oldfd, int newfd)
{
	if (oldfd == newfd || newfd < 0 || oldfd < 0)
		return -1;

	struct fd_elem *oldfd_elem = fd_find(oldfd);
	struct fd_elem *newfd_elem = fd_find(newfd);

	if (!oldfd_elem)
		return -1;

	/* 이미 같은 파일을 참조한다면 종료*/
	if (newfd_elem && oldfd_elem->ref_file_elem == newfd_elem->ref_file_elem)
		return newfd;

	/* newfd를 old_fd_elem이 참조하는 file_elem에 연결 */
	if (!register_fd(oldfd_elem->ref_file_elem, newfd))
		return -1;

	fd_close(newfd_elem); // 기존 newfd close

	return newfd;
}

/* fd를 해당 file_elem에 연결하고 fd_elem 구조체 반환 */
struct fd_elem *register_fd(struct file_elem *file_elem, int fd)
{
	/* 새로운 fd_elem 생성 */
	struct fd_elem *fd_elem = calloc(1, sizeof(struct fd_elem));
	if (!fd_elem)
		return NULL;

	/* file_elem->fd_list에 fd_elem 추가 */
	fd_elem->fd = fd;
	fd_elem->ref_file_elem = file_elem;
	list_push_back(&file_elem->fd_list, &fd_elem->elem);
	return fd_elem;
}

/* 새로운 file_elem 생성 후 반환  */
struct file_elem *new_file_elem()
{
	/* 새로운 file_elem 생성 */
	struct file_elem *file_elem = calloc(1, sizeof(struct file_elem));
	if (file_elem == NULL)
		return NULL;

	list_init(&file_elem->fd_list); // fd_list 초기화
	return file_elem;
}

/* fd에 할당된 파일 반환 */
struct file *fd_get_file(int fd)
{
	struct fd_elem *_fd = fd_find(fd); // fd_elem 탐색
	if (!_fd)
		return NULL;
	return _fd->ref_file_elem->file;
}

/* fd에 해당하는 fd_elem 구조체 반환 */
struct fd_elem *fd_find(int fd)
{
	if (fd < 0)
		return NULL;

	struct thread *curr = thread_current();
	struct list *pool = &curr->fd_pool;
	struct list_elem *p = list_begin(pool);
	while (p != list_end(pool)) // fd_pool 내 file_elem을 순회
	{
		struct file_elem *_file = list_entry(p, struct file_elem, elem);
		struct list *fd_list = &_file->fd_list;
		struct list_elem *n = list_begin(fd_list);
		while (n != list_end(fd_list)) // file_elem의 fd_list 내 fd_elem을 순회
		{
			struct fd_elem *f = list_entry(n, struct fd_elem, elem);
			if (f->fd == fd) // fd를 가진 fd_elem 탐색 시 반환
			{
				return f;
			}
			n = n->next;
		}
		p = p->next;
	}
	return NULL;
}

/* fd_elem을 닫음 */
void fd_close(struct fd_elem *fd_elem)
{
	if (fd_elem)
	{
		struct thread *curr = thread_current();
		struct file_elem *file_elem = fd_elem->ref_file_elem;

		/* nextfd 갱신 */
		if (curr->nextfd > fd_elem->fd)
			curr->nextfd = fd_elem->fd;

		/* fd_elem 제거 */
		list_remove(&fd_elem->elem);
		free(fd_elem);

		/* 참조하는 fd_elem이 없는 file_elem 제거 */
		if (list_empty(&file_elem->fd_list))
		{
			if (is_file(file_elem->file))
				file_close(file_elem->file);

			list_remove(&file_elem->elem);
			free(file_elem);
		}
	}
}