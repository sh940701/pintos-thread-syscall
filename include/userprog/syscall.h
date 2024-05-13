#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <list.h>

/* stdin,stdout 구분값 */
#define FD_STDIN 0xFD00000 + 1
#define FD_STDOUT 0xFD00000 + 2

/* 파일 디스크립터 최대 갯수 */
#define FD_MAX 128

/* file 확인 */
#define is_file(file) (((file) != FD_STDIN) && ((file) != FD_STDOUT))

/* file_elem 구조체
 * 프로세스의 fd_pool내 파일을 저장해놓음
 * fd_list에 이 파일을 참조하는 fd_elem 저장 */
struct file_elem
{
    struct file *file;
    struct list fd_list;
    struct list_elem elem;
};

/* fd_elem 구조체
 * 파일 디스크립터의 구조체
 * file_elem을 참조하여 파일을 가져옴 */
struct fd_elem
{
    struct file_elem *ref_file_elem;
    struct list_elem elem;
    int fd;
};

void syscall_init(void);

/* 파일 디스크립터 테이블 관련 함수 */

struct fd_elem *fd_find(int fd_elem);
struct file *fd_get_file(int fd);
struct file_elem *new_file_elem();
struct fd_elem *register_fd(struct file_elem *file_elem, int fd);

#endif /* userprog/syscall.h */
