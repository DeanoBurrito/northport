#pragma once

#include <stdint.h>

//these symbols map to the virtual addresses of the kernel sections.
extern "C"
{
    extern uint8_t KERNEL_TEXT_BEGIN[];
    extern uint8_t KERNEL_TEXT_END[];
    extern uint8_t KERNEL_RODATA_BEGIN[];
    extern uint8_t KERNEL_RODATA_END[];
    extern uint8_t KERNEL_DATA_BEGIN[];
    extern uint8_t KERNEL_DATA_END[];
}
