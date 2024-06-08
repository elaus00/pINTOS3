#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
/* for shutdown_power_off */
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include "devices/input.h"
#include "vm/page.h"

#ifdef VM
#include "vm/page.h"
#endif

struct file_descriptor
{
  int fd_num;
  tid_t owner;
  struct file *file_struct;
  struct list_elem elem;
};

/* a list of open files, represents all the files open by the user process
   through syscalls. */
struct list open_files;

/* the lock used by syscalls involving file system to ensure only one thread
   at a time is accessing file system */
struct lock fs_lock;

static void syscall_handler(struct intr_frame *);

/* vm 구현하는데 필요한 것들 */
static void check_user(const uint8_t *uaddr);
static int32_t get_user(const uint8_t *uaddr);
static bool put_user(uint8_t *udst, uint8_t byte);
static int memread_user(void *src, void *des, size_t bytes);

/* System call functions */
static void halt(void);
static void exit(int);
static pid_t exec(const char *);
static int wait(pid_t);
static bool create(const char *, unsigned);
static bool remove(const char *);
static int open(const char *);
static int filesize(int);
static int read(int, void *, unsigned);
static int write(int, const void *, unsigned);
static void seek(int, unsigned);
static unsigned tell(int);
static void close(int);
static int memread_user(void *src, void *des, size_t bytes);

enum fd_search_filter
{
  FD_FILE = 1,
  FD_DIRECTORY = 2
};
static struct file_desc *find_file_desc(struct thread *, int fd, enum fd_search_filter flag);
/* End of system call functions */

static int32_t get_user(const uint8_t *uaddr);
static bool put_user(uint8_t *udst, uint8_t byte);
static void check_user(const uint8_t *uaddr);
static void fail_invalid_access(void);
static int memread_user(void *src, void *dst, size_t bytes);
static struct mmap_desc *find_mmap_desc(struct thread *t, mmapid_t mid);
void preload_and_pin_pages(const void *buffer, size_t size);
void unpin_preloaded_pages(const void *buffer, size_t size);

static struct file_descriptor *get_open_file(int);
static void close_open_file(int);
bool is_valid_ptr(const void *);
static int allocate_fd(void);
void close_file_by_owner(tid_t);

#ifdef VM
mmapid_t sys_mmap(int fd, void *);
bool sys_munmap(mmapid_t);

static struct mmap_desc *find_mmap_desc(struct thread *, mmapid_t fd);

void preload_and_pin_pages(const void *, size_t);
void unpin_preloaded_pages(const void *, size_t);
#endif

void syscall_init(void)
{
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
  list_init(&open_files);
  lock_init(&fs_lock);
}

static void
syscall_handler(struct intr_frame *f)
{
  uint32_t *esp;
  esp = f->esp;

  if (!is_valid_ptr(esp) || !is_valid_ptr(esp + 1) ||
      !is_valid_ptr(esp + 2) || !is_valid_ptr(esp + 3))
  {
    exit(-1);
  }
  else
  {
    int syscall_number = *esp;
    switch (syscall_number)
    {
    case SYS_HALT:
      halt();
      break;
    case SYS_EXIT:
      exit(*(esp + 1));
      break;
    case SYS_EXEC:
      f->eax = exec((char *)*(esp + 1));
      break;
    case SYS_WAIT:
      f->eax = wait(*(esp + 1));
      break;
    case SYS_CREATE:
      f->eax = create((char *)*(esp + 1), *(esp + 2));
      break;
    case SYS_REMOVE:
      f->eax = remove((char *)*(esp + 1));
      break;
    case SYS_OPEN:
      f->eax = open((char *)*(esp + 1));
      break;
    case SYS_FILESIZE:
      f->eax = filesize(*(esp + 1));
      break;
    case SYS_READ:
      f->eax = read(*(esp + 1), (void *)*(esp + 2), *(esp + 3));
      break;
    case SYS_WRITE:
      f->eax = write(*(esp + 1), (void *)*(esp + 2), *(esp + 3));
      break;
    case SYS_SEEK:
      seek(*(esp + 1), *(esp + 2));
      break;
    case SYS_TELL:
      f->eax = tell(*(esp + 1));
      break;
    case SYS_CLOSE:
    {
      close(*(esp + 1));
      break;
    }
#ifdef VM
    case SYS_MMAP:
    {
      int fd;
      void *addr;
      memread_user(f->esp + 4, &fd, sizeof(fd));
      memread_user(f->esp + 8, &addr, sizeof(addr));

      mmapid_t ret = sys_mmap(fd, addr);
      f->eax = ret;
      break;
    }
    case SYS_MUNMAP:
    {
      mmapid_t mid;
      memread_user(f->esp + 4, &mid, sizeof(mid));
      sys_munmap(mid);
      break;
    }
#endif

    default:
      break;
    }
  }
}

/* Terminates the current user program, returning status to the kernel.
 */
void exit(int status)
{
  /* later on, we need to determine if there is process waiting for it */
  /* process_exit (); */
  struct child_status *child;
  struct thread *cur = thread_current();
  struct thread *parent = NULL;
  printf("%s: exit(%d)\n", cur->name, status);
  parent = thread_get_by_id(cur->parent_id);
  if (parent != NULL)
  {
    struct list_elem *e = list_tail(&parent->children);
    while ((e = list_prev(e)) != list_head(&parent->children))
    {
      child = list_entry(e, struct child_status, elem_child_status);
      if (child->child_id == cur->tid)
      {
        lock_acquire(&parent->lock_child);
        child->is_exit_called = true;
        child->child_exit_status = status;
        lock_release(&parent->lock_child);
      }
    }
  }
  thread_exit();
}

void halt(void)
{
  shutdown_power_off();
}

pid_t exec(const char *cmd_line)
{
  /* a thread's id. When there is a user process within a kernel thread, we
   * use one-to-one mapping from tid to pid, which means pid = tid
   */
  tid_t tid;
  struct thread *cur;
  /* check if the user pinter is valid */
  if (!is_valid_ptr(cmd_line))
  {
    exit(-1);
  }

  cur = thread_current();

  cur->child_load_status = 0;
  tid = process_execute(cmd_line);
  lock_acquire(&cur->lock_child);
  while (cur->child_load_status == 0)
    cond_wait(&cur->cond_child, &cur->lock_child);
  if (cur->child_load_status == -1)
    tid = -1;
  lock_release(&cur->lock_child);
  return tid;
}

int wait(pid_t pid)
{
  return process_wait(pid);
}

bool create(const char *file_name, unsigned size)
{
  bool status;

  if (!is_valid_ptr(file_name))
    exit(-1);

  lock_acquire(&fs_lock);
  status = filesys_create(file_name, size);
  lock_release(&fs_lock);
  return status;
}

bool remove(const char *file_name)
{
  bool status;

  if (!is_valid_ptr(file_name))
    exit(-1);

  lock_acquire(&fs_lock);
  status = filesys_remove(file_name);
  lock_release(&fs_lock);
  return status;
}

int open(const char *file_name)
{
  struct file *f;
  struct file_descriptor *fd;
  int status = -1;

  if (!is_valid_ptr(file_name))
    exit(-1);

  lock_acquire(&fs_lock);

  f = filesys_open(file_name);
  if (f != NULL)
  {
    fd = calloc(1, sizeof *fd);
    fd->fd_num = allocate_fd();
    fd->owner = thread_current()->tid;
    fd->file_struct = f;
    list_push_back(&open_files, &fd->elem);
    status = fd->fd_num;
  }
  lock_release(&fs_lock);
  return status;
}

int filesize(int fd)
{
  struct file_descriptor *fd_struct;
  int status = -1;
  lock_acquire(&fs_lock);
  fd_struct = get_open_file(fd);
  if (fd_struct != NULL)
    status = file_length(fd_struct->file_struct);
  lock_release(&fs_lock);
  return status;
}

int read(int fd, void *buffer, unsigned size)
{
  struct file_descriptor *fd_struct;
  int status = 0;

  if (!is_valid_ptr(buffer) || !is_valid_ptr(buffer + size - 1))
    exit(-1);

  lock_acquire(&fs_lock);

  if (fd == STDOUT_FILENO)
  {
    lock_release(&fs_lock);
    return -1;
  }

  if (fd == STDIN_FILENO)
  {
    uint8_t c;
    unsigned counter = size;
    uint8_t *buf = buffer;
    while (counter > 1 && (c = input_getc()) != 0)
    {
      *buf = c;
      buffer++;
      counter--;
    }
    *buf = 0;
    lock_release(&fs_lock);
    return (size - counter);
  }

  fd_struct = get_open_file(fd);
  if (fd_struct != NULL)
    status = file_read(fd_struct->file_struct, buffer, size);

  lock_release(&fs_lock);
  return status;
}

int write(int fd, const void *buffer, unsigned size)
{
  struct file_descriptor *fd_struct;
  int status = 0;

  if (!is_valid_ptr(buffer) || !is_valid_ptr(buffer + size - 1))
    exit(-1);

  lock_acquire(&fs_lock);

  if (fd == STDIN_FILENO)
  {
    lock_release(&fs_lock);
    return -1;
  }

  if (fd == STDOUT_FILENO)
  {
    putbuf(buffer, size);
    lock_release(&fs_lock);
    return size;
  }

  fd_struct = get_open_file(fd);
  if (fd_struct != NULL)
    status = file_write(fd_struct->file_struct, buffer, size);
  lock_release(&fs_lock);
  return status;
}

void seek(int fd, unsigned position)
{
  struct file_descriptor *fd_struct;
  lock_acquire(&fs_lock);
  fd_struct = get_open_file(fd);
  if (fd_struct != NULL)
    file_seek(fd_struct->file_struct, position);
  lock_release(&fs_lock);
  return;
}

unsigned
tell(int fd)
{
  struct file_descriptor *fd_struct;
  int status = 0;
  lock_acquire(&fs_lock);
  fd_struct = get_open_file(fd);
  if (fd_struct != NULL)
    status = file_tell(fd_struct->file_struct);
  lock_release(&fs_lock);
  return status;
}

void close(int fd)
{
  struct file_descriptor *fd_struct;
  lock_acquire(&fs_lock);
  fd_struct = get_open_file(fd);
  if (fd_struct != NULL && fd_struct->owner == thread_current()->tid)
    close_open_file(fd);
  lock_release(&fs_lock);
  return;
}

struct file_descriptor *
get_open_file(int fd)
{
  struct list_elem *e;
  struct file_descriptor *fd_struct;
  e = list_tail(&open_files);
  while ((e = list_prev(e)) != list_head(&open_files))
  {
    fd_struct = list_entry(e, struct file_descriptor, elem);
    if (fd_struct->fd_num == fd)
      return fd_struct;
  }
  return NULL;
}

void close_open_file(int fd)
{
  struct list_elem *e;
  struct list_elem *prev;
  struct file_descriptor *fd_struct;
  e = list_end(&open_files);
  while (e != list_head(&open_files))
  {
    prev = list_prev(e);
    fd_struct = list_entry(e, struct file_descriptor, elem);
    if (fd_struct->fd_num == fd)
    {
      list_remove(e);
      file_close(fd_struct->file_struct);
      free(fd_struct);
      return;
    }
    e = prev;
  }
  return;
}

/* The kernel must be very careful about doing so, because the user can
 * pass a null pointer, a pointer to unmapped virtual memory, or a pointer
 * to kernel virtual address space (above PHYS_BASE). All of these types of
 * invalid pointers must be rejected without harm to the kernel or other
 * running processes, by terminating the offending process and freeing
 * its resources.
 */
bool is_valid_ptr(const void *usr_ptr)
{
  struct thread *cur = thread_current();
  if (usr_ptr != NULL && is_user_vaddr(usr_ptr))
  {
    return (pagedir_get_page(cur->pagedir, usr_ptr)) != NULL;
  }
  return false;
}

int allocate_fd()
{
  static int fd_current = 1;
  return ++fd_current;
}

void close_file_by_owner(tid_t tid)
{
  struct list_elem *e;
  struct list_elem *next;
  struct file_descriptor *fd_struct;
  e = list_begin(&open_files);
  while (e != list_tail(&open_files))
  {
    next = list_next(e);
    fd_struct = list_entry(e, struct file_descriptor, elem);
    if (fd_struct->owner == tid)
    {
      list_remove(e);
      file_close(fd_struct->file_struct);
      free(fd_struct);
    }
    e = next;
  }
}

static int32_t get_user(const uint8_t *uaddr)
{
  if (!((void *)uaddr < PHYS_BASE))
  {
    return -1;
  }
  int result;
  asm("movl $1f, %0; movzbl %1, %0; 1:"
      : "=&a"(result) : "m"(*uaddr));
  return result;
}

static bool put_user(uint8_t *udst, uint8_t byte)
{
  if (!((void *)udst < PHYS_BASE))
  {
    return false;
  }
  int error_code;
  asm("movl $1f, %0; movb %b2, %1; 1:" : "=&a"(error_code), "=m"(*udst) : "q"(byte));
  return error_code != -1;
}

static void check_user(const uint8_t *uaddr)
{
  if (get_user(uaddr) == -1)
    fail_invalid_access();
}

static void fail_invalid_access(void)
{
  if (lock_held_by_current_thread(&fs_lock))
    lock_release(&fs_lock);
  exit(-1);
  NOT_REACHED();
}

static int
memread_user(void *src, void *dst, size_t bytes)
{
  int32_t value;
  size_t i;
  for (i = 0; i < bytes; i++)
  {
    value = get_user(src + i);
    if (value == -1) // segfault or invalid memory access
      fail_invalid_access();

    *(char *)(dst + i) = value & 0xff;
  }
  return (int)bytes;
}

static struct mmap_desc *find_mmap_desc(struct thread *t, mmapid_t mid)
{
  ASSERT(t != NULL);
  struct list_elem *e;
  if (!list_empty(&t->mmap_list))
  {
    for (e = list_begin(&t->mmap_list); e != list_end(&t->mmap_list); e = list_next(e))
    {
      struct mmap_desc *desc = list_entry(e, struct mmap_desc, elem);
      if (desc->id == mid)
      {
        return desc;
      }
    }
  }
  return NULL;
}

void preload_and_pin_pages(const void *buffer, size_t size)
{
  struct supplemental_page_table *supt = thread_current()->supt;
  uint32_t *pagedir = thread_current()->pagedir;
  void *upage;
  for (upage = pg_round_down(buffer); upage < buffer + size; upage += PGSIZE)
  {
    vm_load_page(supt, pagedir, upage);
    vm_pin_page(supt, upage);
  }
}

void unpin_preloaded_pages(const void *buffer, size_t size)
{
  struct supplemental_page_table *supt = thread_current()->supt;
  void *upage;
  for (upage = pg_round_down(buffer); upage < buffer + size; upage += PGSIZE)
  {
    vm_unpin_page(supt, upage);
  }
}

#ifdef VM
mmapid_t sys_mmap(int fd, void *upage)
{
  // check arguments
  if (upage == NULL || pg_ofs(upage) != 0)
    return -1;
  if (fd <= 1)
    return -1; // 0 and 1 are unmappable
  struct thread *curr = thread_current();

  lock_acquire(&fs_lock);

  /* 1. Open file */
  struct file *f = NULL;
  struct file_desc *file_d = find_file_desc(thread_current(), fd, FD_FILE);
  if (file_d && file_d->file)
  {
    f = file_reopen(file_d->file);
  }
  if (f == NULL)
    goto MMAP_FAIL;

  size_t file_size = file_length(f);
  if (file_size == 0)
    goto MMAP_FAIL;

  /* 2. Mapping memory pages */
  // First, ensure that all the page address is NON-EXIESENT.
  size_t offset;
  for (offset = 0; offset < file_size; offset += PGSIZE)
  {
    void *addr = upage + offset;
    if (vm_supt_has_entry(curr->supt, addr))
      goto MMAP_FAIL;
  }

  // Now, map each page to filesystem
  for (offset = 0; offset < file_size; offset += PGSIZE)
  {
    void *addr = upage + offset;

    size_t read_bytes = (offset + PGSIZE < file_size ? PGSIZE : file_size - offset);
    size_t zero_bytes = PGSIZE - read_bytes;

    vm_supt_install_filesys(curr->supt, addr,
                            f, offset, read_bytes, zero_bytes, /*writable*/ true);
  }

  /* 3. Assign mmapid */
  mmapid_t mid;
  if (!list_empty(&curr->mmap_list))
  {
    mid = list_entry(list_back(&curr->mmap_list), struct mmap_desc, elem)->id + 1;
  }
  else
    mid = 1;

  struct mmap_desc *mmap_d = (struct mmap_desc *)malloc(sizeof(struct mmap_desc));
  mmap_d->id = mid;
  mmap_d->file = f;
  mmap_d->addr = upage;
  mmap_d->size = file_size;
  list_push_back(&curr->mmap_list, &mmap_d->elem);

  // OK, release and return the mid
  lock_release(&fs_lock);
  return mid;

MMAP_FAIL:
  // finally: release and return
  lock_release(&fs_lock);
  return -1;
}

bool sys_munmap(mmapid_t mid)
{
  struct thread *curr = thread_current();
  struct mmap_desc *mmap_d = find_mmap_desc(curr, mid);

  if (mmap_d == NULL)
  {               // not found such mid
    return false; // or fail_invalid_access() ?
  }

  lock_acquire(&fs_lock);
  {
    // Iterate through each page
    size_t offset, file_size = mmap_d->size;
    for (offset = 0; offset < file_size; offset += PGSIZE)
    {
      void *addr = mmap_d->addr + offset;
      size_t bytes = (offset + PGSIZE < file_size ? PGSIZE : file_size - offset);
      vm_supt_mm_unmap(curr->supt, curr->pagedir, addr, mmap_d->file, offset, bytes);
    }

    // Free resources, and remove from the list
    list_remove(&mmap_d->elem);
    file_close(mmap_d->file);
    free(mmap_d);
  }
  lock_release(&fs_lock);

  return true;
}

#endif

static struct file_desc *
find_file_desc(struct thread *t, int fd, enum fd_search_filter flag)
{
  ASSERT(t != NULL);

  if (fd < 3)
  {
    return NULL;
  }

  struct list_elem *e;

  if (!list_empty(&t->file_descriptors))
  {
    for (e = list_begin(&t->file_descriptors);
         e != list_end(&t->file_descriptors); e = list_next(e))
    {
      struct file_desc *desc = list_entry(e, struct file_desc, elem);
      if (desc->id == fd)
      {
        // found. filter by flag to distinguish file and directorys
        if (desc->dir != NULL && (flag & FD_DIRECTORY))
          return desc;
        else if (desc->dir == NULL && (flag & FD_FILE))
          return desc;
      }
    }
  }

  return NULL; // not found
}