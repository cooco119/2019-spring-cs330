#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/disk.h"
#include "threads/thread.h"

/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  filesys_disk = disk_get (0, 1);
  if (filesys_disk == NULL)
    PANIC ("hd0:1 (hdb) not present, file system initialization failed");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
  // thread_create("write-behind", PRI_MAX, write_behind_helper, thread_current());
  thread_current()->current_directory = dir_open_root();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
  // thread_current()->kill_child = true;
  // sema_down(&write_behind_lock);
  free_buffer_cache();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (char *name, off_t initial_size, bool is_dir) 
{
  disk_sector_t inode_sector = 0;
  
  struct dir *dir;  

  char *name_cpy = malloc(sizeof (char) * strlen(name) + 1);
  strlcpy(name_cpy, name, strlen(name) + 1);
  char *passing_name;
  size_t token_len;
  char *token, *save_ptr;
  // printf("name: %s, is_dir: %s, debug: %p\n", name, is_dir ? "true" : "false", token);
  // if (strcmp(name, "a/b") == 0){
  //   passing_name[1] = '\0';
  //   printf("name change : %s\n", passing_name);
  // }

  token = strtok_r (name_cpy, "/", &save_ptr);
  // printf("hello\n");
  if (token == ".")
  {
    dir = thread_current()->current_directory;
  }
  else if (token == "..")
  {
    dir = thread_current()->parent_directory;
  }
  else // start with / or nothing
  {
    // printf("In root dir\n");
    dir = dir_open_root();
  }

  /* pass parsed name */
  passing_name = name_cpy;
  if (strlen(save_ptr) != 0)
    passing_name = save_ptr;
  // printf("passing name: %s, original name: %s\n", passing_name, name_cpy);

  bool success = (dir != NULL);
                // printf("create : dir %s\n", success ? "success" : "failed");
                success = success  && free_map_allocate (1, &inode_sector);
                // printf("created inode at %d\n", inode_sector);
                // printf("create : freemap alloc %s\n", success ? "success" : "failed");
                success = success  && inode_create (inode_sector, initial_size, is_dir);
                // printf("create : inode_create %s\n", success ? "success" : "failed");
                success = success  && dir_add (dir, passing_name, inode_sector, is_dir);
                // printf("create : dir_add %s\n", success ? "success" : "failed");
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  free(name_cpy);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  /* TODO
  
    This is only for absolute path,
    Should support relative path (start with . or ..)
    Pass name starting without / or . or ..
  */
  struct dir *dir;  

  char *name_cpy = malloc(sizeof (char) * strlen(name) + 1);
  strlcpy(name_cpy, name, strlen(name) + 1);
  char *passing_name;
  size_t token_len;
  char *token, *save_ptr;

  token = strtok_r (name_cpy, "/", &save_ptr);
  // printf("token %s\n", token);

  // if (name_cpy[0] == '/')
  // {
  //   dir = dir_open_root();
  //   if (strlen(save_ptr) == 0)
  //   {
  //     return file_open(dir_get_inode(dir) );
  //   }
  // }
  // else
  // {
  //   if (thread_current()->current_directory == NULL)
  //     dir = dir_open_root();
  //   else
  //   {
  //     dir = dir_reopen(thread_current()->current_directory);
  //     if (strlen(save_ptr) == 0)
  //     {
  //       return file_open(dir_get_inode(dir) );
  //     }
  //   }
    
  // }
  

  if (token == ".")
  {
    // printf("curr directory\n");
    dir = thread_current()->current_directory;
    if (strlen(save_ptr) == 0)
      return file_open(dir_get_inode(dir));
  }
  else if (token == "..")
  {
    dir = thread_current()->parent_directory;
    if (strlen(save_ptr) == 0)
      return file_open(dir_get_inode(dir));
  }
  else if (name_cpy[0] == '.' && thread_current()->current_directory != NULL)
  {
    dir = dir_reopen (thread_current()->current_directory);
    if (strlen(save_ptr) == 0)
    {
      return file_open(dir_get_inode(dir) );
    }
  }
  else // start with / or nothing
  {
    // printf("Opening root dir\n");
    dir = dir_open_root();
  }
  
  if (token == NULL)
  {
    return file_open(dir_get_inode(dir) );
  }

  /* pass parsed name */
  passing_name = name_cpy;
  if (strlen(save_ptr) != 0)
    passing_name = save_ptr;

  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, passing_name, &inode);
  dir_close (dir);

  return file_open (inode);
}

struct dir *
filesys_open_dir (const char *name)
{
  /* TODO
  
    This is only for absolute path,
    Should support relative path (start with . or ..)
    Pass name starting without / or . or ..
  */
  struct dir *dir;  

  char *passing_name;
  size_t token_len = 0;
  char *token, *save_ptr = malloc(sizeof(char));

  token = strtok_r (name, "/", &save_ptr);
  if (token == ".")
  {
    dir = thread_current()->current_directory;
  }
  else if (token == "..")
  {
    dir = thread_current()->parent_directory;
  }
  else // start with / or nothing
  {
    dir = dir_open_root();
  }

  if (token == NULL)
  {
    return file_open(dir_get_inode(dir) );
  }

  /* pass parsed name */
  if (token_len != NULL)
    token_len = strlen(token);
  passing_name = name;
  if (token_len < strlen(name) && token_len != 0)
    passing_name = name + token_len;

  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, passing_name, &inode);

  return dir_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir); 

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  // free_map_close ();
  printf ("done.\n");
}
