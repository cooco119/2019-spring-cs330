#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/file.h"

struct file 
  {
    struct inode *inode;        /* File's inode. */
    off_t pos;                  /* Current position. */
    bool deny_write;            /* Has file_deny_write() been called? */
  };

static void syscall_handler (struct intr_frame *);

void halt (void);
int exit (int status);
tid_t exec (const char *cmd_line);
int wait (tid_t tid);
bool create (const char *file, unsigned initial_size);
bool remove_file (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  // printf ("system call!\n");
  // printf ("syscal num: %d\n", *(uint32_t*) (f->esp));
  
  switch (*(uint32_t *)(f->esp)) {
    case SYS_HALT:                   /* Halt the operating system. */
      halt ();
      break;
    case SYS_EXIT:                   /* Terminate this process. */
      if (! is_user_vaddr(f->esp + 4)){
        exit(-1);
      }
      exit(*(uint32_t*) (f->esp + 4));
      break;
    case SYS_EXEC:                   /* Start another process. */
      if (! is_user_vaddr(f->esp + 4)){
        exit(-1);
      }
      f->eax = exec(*(uint32_t*) (f->esp + 4));
      break;
    case SYS_WAIT:                   /* Wait for a child process to die. */
      if (! is_user_vaddr(f->esp + 4)){
        exit(-1);
      }
      f->eax = wait( (tid_t)*(uint32_t*)(f->esp + 4));
      break;
    case SYS_CREATE:                 /* Create a file. */
      if (! is_user_vaddr(f->esp + 4) || ! is_user_vaddr(f->esp + 8)){
        exit(-1);
      }
      f->eax = create ((const char *) *(uint32_t*) (f->esp + 4), (unsigned int) *(uint32_t*) (f->esp + 8));
      break;
    case SYS_REMOVE:                 /* Delete a file. */
      if (! is_user_vaddr(f->esp + 4)){
        exit(-1);
      }
      f->eax = remove_file ((const char *) *(uint32_t*) (f->esp + 4));
      break;
    case SYS_OPEN:                   /* Open a file. */
      if (! is_user_vaddr(f->esp + 4)){
        exit(-1);
      }
      f->eax = open((const char *)*(uint32_t*) (f->esp + 4));
      break;
    case SYS_FILESIZE:               /* Obtain a file's size. */
      if (! is_user_vaddr(f->esp + 4)){
        exit(-1);
      }
      f->eax = filesize ((int) *(uint32_t*) (f->esp + 4));
      break;
    case SYS_READ:                   /* Read from a file. */
      if (! is_user_vaddr(f->esp + 4) || !is_user_vaddr(f->esp + 8) || !is_user_vaddr(f->esp + 12)){
        exit(-1);
      }
      f->eax = read ((int) *(uint32_t*) (f->esp + 4), (void *) *(uint32_t*) (f->esp + 8),
            (unsigned) *(uint32_t*) (f->esp + 12));
      break;
    case SYS_WRITE:                  /* Write to a file. */
      if (! is_user_vaddr(f->esp + 4) || !is_user_vaddr(f->esp + 8) || !is_user_vaddr(f->esp + 12)){
        exit(-1);
      }
      f->eax = write ((int) *(uint32_t*) (f->esp + 4), (void *) *(uint32_t*) (f->esp + 8), 
              (unsigned) *(uint32_t*) (f->esp + 12));
      break;
    case SYS_SEEK:                   /* Change position in a file. */
      if (! is_user_vaddr(f->esp + 4) || ! is_user_vaddr(f->esp + 8)){
        exit(-1);
      }
      seek ((int) *(uint32_t*) (f->esp + 4), (unsigned int) *(uint32_t*) (f->esp + 8));
      break;
    case SYS_TELL:                   /* Report current position in a file. */
      if (! is_user_vaddr(f->esp + 4)){
        exit(-1);
      }
      f->eax = tell ((int) *(uint32_t*) (f->esp + 4));
      break;
    case SYS_CLOSE:
      if (! is_user_vaddr(f->esp + 4)){
        exit(-1);
      }
      close ((int) *(uint32_t*) (f->esp + 4));
      break;
    default:
      break; 
  }
  // thread_exit ();
}

void halt(void) {
  power_off();
}

int exit(int status) {
  int i = 0;
  printf("%s: exit(%d)\n", thread_name(), status);
  thread_current()->exit_code = status;
  for (i = 3; i < 128; i++) {
    if (thread_current()->files[i] != NULL) {
      close(i);
    }
  }
  thread_exit();
}

tid_t exec (const char *cmd_line) {
  tid_t child_tid;
  // printf("cmd_line: %s\n", cmd_line);
  child_tid = process_execute(cmd_line);
  return child_tid;
}

int wait (tid_t tid) {
  return process_wait(tid);
}

bool create (const char *file, unsigned initial_size) {
  if (file == NULL) {
    exit(-1);
  }
  return filesys_create(file, initial_size);
}

bool remove_file (const char *file) {
  return filesys_remove(file);
}

int open (const char *file) {
  int i = 0;
  if (file == NULL){
    exit(-1);
  }
  struct file *open_file = filesys_open(file);
  if (open_file == NULL) {
    return -1;
  }
  else {
    for (i = 3; i < 128; i++) {
      if (thread_current()->files[i] == NULL) {
        // printf("filename: %s, threadname: %s\n", file, thread_current()->name);
        if (strcmp(thread_current()->name, file) == 0){
          file_deny_write(open_file);
          // printf("denied write to %s\n", file);
        }
        thread_current()->files[i] = open_file;
        return i;
      }
    }
  }
}

int filesize (int fd) {
  return file_length(thread_current()->files[fd]);
}

int read (int fd, void *buffer, unsigned size) {
  int i;
  int STDIN = 0;

  if (fd == STDIN) {
    char *tmp = malloc(sizeof(char) * size);
    for (i = 0; i < size; i++) {
      tmp[i] = (char) input_getc();
      if (tmp[i] == '\0') {
        break;
      }
    }
    buffer = (void *) tmp;
    return i;
  }
  else if (fd >= 3) {
    if (thread_current()->files[fd] == NULL) {
      return exit(-1);
    }
    return file_read(thread_current()->files[fd], buffer, size);
  }
}

int write (int fd, const void *buffer, unsigned size) {
  int STDOUT = 1;

  if (fd == STDOUT) {
    putbuf (buffer, size);
    return size;
  }
  else if (fd >= 3) {
    return file_write(thread_current()->files[fd], buffer, size);
  }

  return -1;
}

void seek (int fd, unsigned position) {
  file_seek(thread_current()->files[fd], position);
}

unsigned tell (int fd) {
  return file_tell(thread_current()->files[fd]);
}

void close (int fd){
  file_close(thread_current()->files[fd]);
  thread_current()->files[fd] = NULL;
}