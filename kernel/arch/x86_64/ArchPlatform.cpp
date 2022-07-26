#include <arch/x86_64/ArchPlatform.h>
#include <BufferView.h>

namespace Kernel
{
    NativeUInt hhdmBase;
    NativeUInt hhdmLength;
    
    sl::BufferView GetHhdm()
    { return { hhdmBase, hhdmLength }; }
    
    void PortWrite8(uint16_t port, uint8_t data)
    {
        asm volatile("outb %0, %1" :: "a"(data), "Nd"(port));
    }

    void PortWrite16(uint16_t port, uint16_t data)
    {
        asm volatile("outw %0, %1" :: "a"(data), "Nd"(port));
    }

    void PortWrite32(uint16_t port, uint32_t data)
    {
        asm volatile("outl %0, %1" :: "a"(data), "Nd"(port));
    }

    uint8_t PortRead8(uint16_t port)
    {
        uint8_t value;
        asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    }

    uint16_t PortRead16(uint16_t port)
    {
        uint16_t value;
        asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    }

    uint32_t PortRead32(uint16_t port)
    {
        uint32_t value;
        asm volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    }

    void WriteMsr(uint32_t address, uint64_t data)
    {
        asm volatile("wrmsr" :: "a"((uint32_t)data), "d"(data >> 32), "c"(address));
    }

    uint64_t ReadMsr(uint32_t address)
    {
        uint32_t high, low;
        asm volatile("rdmsr": "=a"(low), "=d"(high) : "c"(address));
        return ((uint64_t)high << 32) | low;
    }
}
