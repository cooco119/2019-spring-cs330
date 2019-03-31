#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

void halt (void);
int exit (int status);
int wait (tid_t tid);
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
      exit(*(uint32_t*) (f->esp + 4));
      break;
    case SYS_EXEC:                   /* Start another process. */
      break;
    case SYS_WAIT:                   /* Wait for a child process to die. */
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
      break;
    case SYS_WRITE:                  /* Write to a file. */
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

int wait (tid_t tid) {
  return process_wait(tid);
}

int write (int fd, const void *buffer, unsigned size) {
  int STDOUT = 1;

  if (fd == STDOUT) {
    putbuf (buffer, size);
    return size;
  }

  return -1;
}