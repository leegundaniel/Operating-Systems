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

static void die(const char *m) { printf("[FAIL] %s\n", m); exit(1); }
static void ok(const char *m)  { printf("[OK] %s\n", m); }
static void expect(int c, const char *m) { if(!c) die(m); ok(m); }

static void freemem_tag(const char *tag) {
  int fm = freemem();
  printf("[FREEMEM] %s -> %d\n", tag, fm);
}

static uint64 checksum(const uint8 *p, int n) {
  uint64 s=0;
  for (int i=0;i<n;i++) s = s*131 + p[i];
  return s;
}

static void print_first3(const uint8 *base) {
  char ch;
  ch = base[0]; printf(" - fd data: %x %x %x\n", base[0], base[1], base[2]); write(1,&ch,1);
  ch = base[1]; write(1,&ch,1);
  ch = base[2]; write(1,&ch,1);
  printf("\n");
}

static void test_anon_populate(void) {
  int fm0 = freemem();
  uint addr = 0;
  int len = 2*PGSIZE;

  uint64 r = mmap(addr, len, 0x1|0x2, 0x1|0x2, -1, 0);
  expect(r!=0, "mmap (anon+populate) ok");

  uint8 *p = (uint8*)r;
  p[0] = 0x11;
  p[PGSIZE] = 0x22;
  expect(p[0]==0x11 && p[PGSIZE]==0x22, "anon+populate R/W OK");

  int fm1 = freemem();
  expect(fm1 <= fm0, "freemem sane after anon+populate");

  expect(munmap((uint)r)==1, "anon+populate munmap ok");
  freemem_tag("after anon+populate");
}

static void test_anon_no_populate(void) {
  int fm0 = freemem();
  uint addr = PGSIZE;
  int len = 2*PGSIZE;

  uint64 r = mmap(addr, len, 0x1|0x2, 0x1, -1, 0);
  expect(r!=0, "mmap (anon no-populate) ok");

  int fm_before = freemem();
  expect(fm_before <= fm0, "freemem unchanged before touch");

  uint8 *p = (uint8*)r;
  p[0] = 0xAB;
  int fm_after1 = freemem();
  expect(fm_after1 < fm_before, "first PF allocated page");

  p[PGSIZE] = 0xCD;
  int fm_after2 = freemem();
  expect(fm_after2 < fm_after1, "second PF allocated page");

  expect(munmap((uint)r)==1, "anon no-populate munmap ok");
  freemem_tag("after anon no-populate");
}

static uint64 test_file_populate_and_sum(void) {
  int fd = open("README", 0);
  expect(fd>=0, "opened README");

  uint addr = 2*PGSIZE;
  int len = 2*PGSIZE;

  uint64 r = mmap(addr, len, 0x1, 0x2, fd, 0);
  expect(r!=0, "file+populate mmap ok");

  volatile uint8 a = ((uint8*)r)[0];
  volatile uint8 b = ((uint8*)r)[PGSIZE];
  (void)a; (void)b;

  print_first3((const uint8*)r);
  uint64 sum = checksum((const uint8*)r, len);

  expect(munmap((uint)r)==1, "file+populate munmap");
  close(fd);
  freemem_tag("after file+populate");
  return sum;
}

static void test_file_no_populate_compare(uint64 expected_sum) {
  int fd = open("README", 0);
  expect(fd>=0, "opened README");

  uint addr = 3*PGSIZE;
  int len = 2*PGSIZE;

  uint64 r = mmap(addr, len, 0x1, 0, fd, 0);
  expect(r!=0, "file no-populate mmap ok");

  volatile uint8 a = ((uint8*)r)[0];
  int fm1 = freemem();
  volatile uint8 b = ((uint8*)r)[PGSIZE];
  int fm2 = freemem();
  (void)a; (void)b;

  expect(freemem() <= fm1, "freemem sane after first PF");
  expect(freemem() <= fm2, "freemem sane after second PF");

  print_first3((const uint8*)r);

  uint64 sum = checksum((const uint8*)r, len);
  expect(sum==expected_sum, "checksums match");

  expect(munmap((uint)r)==1, "file no-populate munmap");
  close(fd);
  freemem_tag("after file no-populate");
}

static void test_fork_file_compare_and_pf(void) {
  int fd = open("README", 0);
  expect(fd>=0, "opened README");

  uint addr = 4*PGSIZE;
  int len = 2*PGSIZE;

  uint64 r = mmap(addr, len, 0x1, 0, fd, 0);
  expect(r!=0, "fork test mmap ok");

  int fm_before = freemem();
  printf("[INFO] parent freemem before fork: %d\n", fm_before);

  int pfd[2];
  expect(pipe(pfd)==0, "pipe ok");

  int pid = fork();
  expect(pid>=0, "fork ok");

  if(pid==0){
    int fm_pre = freemem();
    volatile uint8 c0 = ((uint8*)r)[0];
    volatile uint8 c1 = ((uint8*)r)[PGSIZE];
    (void)c0; (void)c1;

    print_first3((const uint8*)r);
    uint64 csum = checksum((const uint8*)r, len);

    int fm_post = freemem();
    printf("[INFO] child freemem: before=%d after=%d\n", fm_pre, fm_post);

    write(pfd[1], &csum, sizeof(csum));
    close(pfd[1]);
    exit(0);
  }

  close(pfd[1]);
  uint64 csum = 0;
  int n = read(pfd[0], &csum, sizeof(csum));
  expect(n==sizeof(csum), "parent read child sum");
  close(pfd[0]);

  int fm_pre_touch = freemem();
  volatile uint8 p0 = ((uint8*)r)[0];
  volatile uint8 p1 = ((uint8*)r)[PGSIZE];
  (void)p0; (void)p1;

  print_first3((const uint8*)r);
  uint64 psum = checksum((const uint8*)r, len);

  int fm_post_touch = freemem();
  printf("[INFO] parent freemem: before-touch=%d after-touch=%d\n",
         fm_pre_touch, fm_post_touch);

  expect(psum==csum, "parent/child contents match");
  wait(0);

  expect(munmap((uint)r)==1, "fork test munmap");
  close(fd);

  freemem_tag("after fork test");
}

int main() {
  printf("== xv6 mmap/munmap/freemem verification ==\n");
  freemem_tag("start");

  test_anon_populate();
  test_anon_no_populate();

  uint64 sum_pop = test_file_populate_and_sum();
  test_file_no_populate_compare(sum_pop);

  test_fork_file_compare_and_pf();

  printf("\n== ALL TESTS COMPLETED SUCCESSFULLY ==\n");
  exit(0);
}

