#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);

void halt (void);
int exit (int status);
tid_t exec (const char *cmd_line);
int wait (tid_t tid);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);

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
      exec(*(uint32_t*) (f->esp + 4));
      break;
    case SYS_WAIT:                   /* Wait for a child process to die. */
      if (! is_user_vaddr(f->esp + 4)){
        exit(-1);
      }
      wait( (tid_t)*(uint32_t*)(f->esp + 4));
      break;
    case SYS_CREATE:                 /* Create a file. */
      break;
    case SYS_REMOVE:                 /* Delete a file. */
      break;
    case SYS_OPEN:                   /* Open a file. */
      break;
    case SYS_FILESIZE:               /* Obtain a file's size. */
      break;
    case SYS_READ:                   /* Read from a file. */
      if (! is_user_vaddr(f->esp + 4) || !is_user_vaddr(f->esp + 8) || !is_user_vaddr(f->esp + 12)){
        exit(-1);
      }
      read ((int) *(uint32_t*) (f->esp + 4), (void *) *(uint32_t*) (f->esp + 8),
            (unsigned) *(uint32_t*) (f->esp + 12));
      break;
    case SYS_WRITE:                  /* Write to a file. */
      if (! is_user_vaddr(f->esp + 4) || !is_user_vaddr(f->esp + 8) || !is_user_vaddr(f->esp + 12)){
        exit(-1);
      }
      write ((int) *(uint32_t*) (f->esp + 4), (void *) *(uint32_t*) (f->esp + 8), 
              (unsigned) *(uint32_t*) (f->esp + 12));
      break;
    case SYS_SEEK:                   /* Change position in a file. */
      break;
    case SYS_TELL:                   /* Report current position in a file. */
      break;
    case SYS_CLOSE:
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
  printf("%s: exit(%d)\n", thread_name(), status);
  thread_exit();
}

tid_t exec (const char *cmd_line) {
  return process_execute(cmd_line);
}

int wait (tid_t tid) {
  return process_wait(tid);
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
}

int write (int fd, const void *buffer, unsigned size) {
  int STDOUT = 1;

  if (fd == STDOUT) {
    putbuf (buffer, size);
    return size;
  }

  return -1;
}