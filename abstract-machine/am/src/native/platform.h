#ifndef __PLATFORM_H__
#define __PLATFORM_H__

#include <am.h>
#include <unistd.h>
#include <signal.h>
#include <klib.h>
#include <klib-macros.h>

void __am_get_example_uc(Context *r);
void __am_get_intr_sigmask(sigset_t *s);
int __am_is_sigmask_sti(sigset_t *s);
void __am_init_timer_irq();
void __am_pmem_map(void *va, void *pa, int prot);
void __am_pmem_unmap(void *va);

// Size of the alternate signal stack. Not SIGSTKSZ: since glibc 2.34, files
// that define _GNU_SOURCE get SIGSTKSZ = sysconf(_SC_SIGSTKSZ), a run-time
// value that can't size an array (13504 on a 2026 x86 CPU, more than the old
// constant 8192). A fixed size also keeps this struct's layout identical in
// every file, whether or not that file defines _GNU_SOURCE.
#define AM_SIGSTKSZ (64 * 1024)

// per-cpu structure
typedef struct {
  void *vm_head;
  uintptr_t ksp;
  int cpuid;
  Event ev; // similar to cause register in mips/riscv
  uint8_t sigstack[AM_SIGSTKSZ];
} __am_cpu_t;
extern __am_cpu_t *__am_cpu_struct;
#define thiscpu __am_cpu_struct

#endif
