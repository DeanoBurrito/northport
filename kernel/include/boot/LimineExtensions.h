#pragma once
//Please note that these are my extensions to the original protocol,
//and are not affiliated or endorsed by the original authors at all.

#include <boot/Limine.h>

#if __riscv_xlen == 64
struct limine_smp_info {
    uint64_t hart_id;
    uint32_t plic_context;
    uint32_t reserved;
    LIMINE_PTR(limine_goto_address) goto_address;
    uint64_t extra_argument;
};

struct limine_smp_response {
    uint64_t revision;
    uint64_t bsp_hart_id;
    uint64_t cpu_count;
    LIMINE_PTR(struct limine_smp_info **) cpus;
};
#endif

//The original check is suppressed so we can add extra architectures here (riscv)
#if !defined(__x86_64__) && !defined(__aarch64) && __riscv_xlen != 64
    #error Unknown Architecture.
#endif
