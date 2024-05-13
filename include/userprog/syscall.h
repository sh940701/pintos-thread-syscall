#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <list.h>

/* 파일 디스크립터 최대 갯수 */
#define FD_MAX 128

/* file 확인 */
#define is_file(file) (((file) != FD_STDIN) && ((file) != FD_STDOUT))

/* file_type enum
 * type 구분자 */
enum file_type
{
    FD_FILE,  // 파일
    FD_STDIN, // stdin (0)
    FD_STDOUT // stdout (1)
};
/* file_elem 구조체
 * 프로세스의 fd_pool내 파일을 저장해놓음
 * fd_list에 이 파일을 참조하는 fd_elem 저장 */
struct file_elem
{
    struct file *file;     // 파일
    struct list fd_list;   // 이 file_elem을 참조하는 fd_elem 목록
    enum file_type type;   // 파일 유형
    struct list_elem elem; // list_elem
};

/* fd_elem 구조체
 * 파일 디스크립터의 구조체
 * file_elem을 참조하여 파일을 가져옴 */
struct fd_elem
{
    struct file_elem *ref_file_elem; // 참조하는 file_elem
    struct list_elem elem;           // list_elem
    int fd;                          // 파일 디스크립터 번호
};

void syscall_init(void);

/* 파일 디스크립터 테이블 관련 함수 */

struct fd_elem *fd_find(int fd_elem);
struct file_elem *fd_get_file_elem(int fd);
struct file_elem *new_file_elem();
struct fd_elem *register_fd(struct file_elem *file_elem, int fd);

#endif /* userprog/syscall.h */
