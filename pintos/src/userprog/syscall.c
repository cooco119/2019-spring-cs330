#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/file.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "vm/page.h"
#include "userprog/process.h"

// struct file 
//   {
//     struct inode *inode;        /* File's inode. */
//     off_t pos;                  /* Current position. */
//     bool deny_write;            /* Has file_deny_write() been called? */
//   };

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
int mmap(int fd, void *upage);
bool munmap (int id);

struct semaphore file_lock;

void
syscall_init (void) 
{
  sema_init(&file_lock, 1);
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
    case SYS_MMAP:
      if (! is_user_vaddr(f->esp + 4) || ! is_user_vaddr(f->esp + 8)){
        exit(-1);
      }
      f->eax = mmap((int) *(uint32_t*) f->esp + 4, (void*) *(uint32_t*) f->esp + 8);
      break;
    case SYS_MUNMAP:
      if (! is_user_vaddr(f->esp + 4)){
        exit(-1);
      }
      f->eax = munmap((int) *(uint32_t*) f->esp + 4);
      break;
    default:
      break; 
  }
  // thread_exit ();
}

int mmap(int fd, void * upage)
{
  struct thread *curr = thread_current();
  sema_down(&file_lock);

  struct file *f = curr->files[fd];
  f = file_reopen(f);

  size_t file_size = file_length(f);

  size_t ofs; void *addr;
  struct sup_page_table_entry *supt;
  for (ofs = 0; ofs < file_size; ofs += PGSIZE)
  {
    addr = upage + ofs;
    supt = find_page(curr->supt, addr);
    if (supt != NULL)
    {
      goto FAIL;
    }
  }

  size_t read_bytes, zero_bytes;
  for (ofs = 0; ofs < file_size; ofs += PGSIZE)
  {
    addr = upage + ofs;
    read_bytes = (ofs + PGSIZE < file_size) ? PGSIZE : file_size - ofs;
    zero_bytes = PGSIZE - read_bytes;

    install_from_file (&curr->supt, addr, f, ofs, read_bytes, zero_bytes, true);
    
  }

  int id;
  if (! list_empty(&curr->mmap_list))
  {
    id = list_entry(list_back(&curr->mmap_list), struct mmapd, elem)->id + 1;
  }
  else id = 1;

  struct mmapd *mmap_i = (struct mmapd*) malloc(sizeof(struct mmapd));
  mmap_i->id = id;
  mmap_i->file = f;
  mmap_i->addr = upage;
  mmap_i->size = file_size;
  list_push_back(&curr->mmap_list, &mmap_i->elem);

  sema_up(&file_lock);

  return id;
  FAIL:
  return -1;
}

bool munmap (int id)
{
  struct thread *curr = thread_current();
  struct mmapd *mmap_i = NULL;
  struct list_elem *e;
  if (! list_empty(&curr->mmap_list))
  {
    for (e = list_begin(&curr->mmap_list); e != list_end(&curr->mmap_list); e = list_next(e))
    {
      mmap_i = list_entry(e, struct mmapd, elem);
      if (mmap_i->id == id) break;
    }
  }
  if (mmap_i == NULL) return false;
  sema_down(&file_lock);

  size_t ofs, file_size = mmap_i->size;
  void *addr;
  for (ofs = 0; ofs < file_size; ofs += PGSIZE)
  {
    addr = mmap_i->addr + ofs;
    page_unmap(curr->supt, curr->pagedir, addr, mmap_i->file, ofs);
  }

  list_remove(&mmap_i->elem);
  free(mmap_i);

  sema_up(&file_lock);

  return true;

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
  sema_down(&file_lock);
  // printf("Opening %s\n", file);
  struct file *open_file = filesys_open(file);
  if (open_file == NULL) {
    sema_up(&file_lock);
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
        sema_up(&file_lock);
        return i;
        break;
      }
    }
  }
  // printf("Releasing lock\n");
  sema_up(&file_lock);
  return -1;
}

int filesize (int fd) {
  return file_length(thread_current()->files[fd]);
}

int read (int fd, void *buffer, unsigned size) {
  int i;
  int STDIN = 0;

  sema_down(&file_lock);
  if (fd == STDIN) {
    char *tmp = malloc(sizeof(char) * size);
    for (i = 0; i < size; i++) {
      tmp[i] = (char) input_getc();
      if (tmp[i] == '\0') {
        break;
      }
    }
    buffer = (void *) tmp;
    sema_up(&file_lock);
    return i;
  }
  else if (fd >= 3) {
    if (thread_current()->files[fd] == NULL) {
      sema_up(&file_lock);
      return exit(-1);
    }
    sema_up(&file_lock);
    return file_read(thread_current()->files[fd], buffer, size);
  }
  sema_up(&file_lock);
  return -1;
}

int write (int fd, const void *buffer, unsigned size) {
  int STDOUT = 1;

  sema_down(&file_lock);
  if (fd == STDOUT) {
    putbuf (buffer, size);
    sema_up(&file_lock);
    return size;
  }
  else if (fd >= 3) {
    sema_up(&file_lock);
    return file_write(thread_current()->files[fd], buffer, size);
  }

  sema_up(&file_lock);
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