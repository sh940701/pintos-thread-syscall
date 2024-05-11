#ifndef FILESYS_FILE_H
#define FILESYS_FILE_H

#include "filesys/off_t.h"

struct inode;

#define FD_STDIN 0x10000
#define FD_STDOUT 0x10001

#define FD_CHECK1(fd) (((fd) < FDT_SIZE) && ((fd) >= 0))
#define FD_CHECK2(file) ((file) != NULL)

#define is_file_descryptor(file, fd) (FD_CHECK1(fd) && FD_CHECK2(file))
#define is_real_file(file) (((file) != FD_STDIN) && ((file) != FD_STDOUT))

/* System call : dup2 */
// void file_add_count(struct file *file);
// unsigned get_file_count(struct file *file);

/* Opening and closing files. */
struct file *file_open(struct inode *);
struct file *file_reopen(struct file *);
struct file *file_duplicate(struct file *file);
void file_close(struct file *);
struct inode *file_get_inode(struct file *);

/* Reading and writing. */
off_t file_read(struct file *, void *, off_t);
off_t file_read_at(struct file *, void *, off_t size, off_t start);
off_t file_write(struct file *, const void *, off_t);
off_t file_write_at(struct file *, const void *, off_t size, off_t start);

/* Preventing writes. */
void file_deny_write(struct file *);
void file_allow_write(struct file *);

/* File position. */
void file_seek(struct file *, off_t);
off_t file_tell(struct file *);
off_t file_length(struct file *);

#endif /* filesys/file.h */
