#include "../kernel/types.h"
#include "../kernel/stat.h"
#include "user.h"
#include "../kernel/fcntl.h"
#include "../kernel/memlayout.h"
#include "../kernel/param.h"
#include "../kernel/spinlock.h"
#include "../kernel/sleeplock.h"
#include "../kernel/fs.h"
#include "../kernel/syscall.h"

#define PGSIZE 4096

int
main(void)
{
  printf("== MMAP TESTS ==\n");

  int fm_start = freemem();
  printf("freemem start = %d\n", fm_start);

  // anonymous
  uint64 a = mmap(0, PGSIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);   // anon, RW
  if (a == 0) { printf("anon mmap failed\n"); exit(1); }

  ((char*)a)[0] = 'A';
  ((char*)a)[1] = 'B';
  ((char*)a)[2] = 'C';

  printf("anon mmap bytes: %c %c %c\n",
         ((char*)a)[0], ((char*)a)[1], ((char*)a)[2]);

  munmap((uint)a);
  printf("freemem after anon = %d\n", freemem());


  // file read
  int fd = open("README", 0);
  if (fd < 0) { printf("open README failed\n"); exit(1); }

  uint64 f0 = mmap(PGSIZE, PGSIZE, PROT_READ, 0, fd, 0);
  if (f0 == 0) { printf("file mmap failed\n"); exit(1); }

  printf("file mmap[0]: %c %c %c\n",
         ((char*)f0)[0],
         ((char*)f0)[1],
         ((char*)f0)[2]);

  munmap((uint)f0);
  close(fd);
  printf("freemem after file-mmap = %d\n", freemem());

  // offset
  /*  
  fd = open("README2", 0);
  if (fd < 0) { printf("open README2 failed\n"); exit(1); }

  uint64 f1 = mmap(2*PGSIZE, PGSIZE, PROT_READ, 0, fd, PGSIZE);
  if (f1 == 0) { printf("file mmap offset failed\n"); exit(1); }

  printf("file mmap offset bytes: %c %c %c\n",
         ((char*)f1)[0],
         ((char*)f1)[1],
         ((char*)f1)[2]);

  munmap((uint)f1);
  close(fd);
  printf("freemem after offset-mmap = %d\n", freemem());
  */
  // fork
  fd = open("README", 0);
  if (fd < 0) { printf("open README failed\n"); exit(1); }

  uint64 f2 = mmap(3*PGSIZE, PGSIZE, PROT_READ, 0, fd, 0);
  if (f2 == 0) { printf("fork mmap failed\n"); exit(1); }

  int pid = fork();
  if (pid < 0) { printf("fork failed\n"); exit(1); }

  if (pid == 0) {
    printf("[child] file bytes: %c %c %c\n",
           ((char*)f2)[0], ((char*)f2)[1], ((char*)f2)[2]);
    exit(0);
  }

  wait(0);
  printf("[parent] file bytes: %c %c %c\n",
         ((char*)f2)[0], ((char*)f2)[1], ((char*)f2)[2]);

  munmap((uint)f2);
  close(fd);

  printf("freemem end = %d\n", freemem());
  printf("== TESTS DONE ==\n");

  exit(0);
}
