#include <uacpi/kernel_api.h>
#include <interfaces/driver/Memory.h>
#include <Log.h>
#include <NativePtr.h>
#include <Maths.h>

extern "C"
{
#ifdef __x86_64__
    static uacpi_u64 PortRead(uint16_t port, uacpi_u8 width)
    {
        union
        {
            uint8_t u8;
            uint16_t u16;
            uint32_t u32;
        } temp;

        switch (width)
        {
        case 1: asm("inb %1, %0" : "=a"(temp.u8) : "Nd"(port) : "memory"); break;
        case 2: asm("inw %1, %0" : "=a"(temp.u16) : "Nd"(port) : "memory"); break;
        case 4: asm("inl %1, %0" : "=a"(temp.u32) : "Nd"(port) : "memory"); break;
        }

        return temp.u32;
    }

    static void PortWrite(uint16_t port, uacpi_u8 width, uacpi_u64 data)
    {        
        switch (width)
        {
        case 1: asm volatile("outb %0, %1" :: "a"((uint8_t)data), "Nd"(port)); break;
        case 2: asm volatile("outw %0, %1" :: "a"((uint16_t)data), "Nd"(port)); break;
        case 4: asm volatile("outl %0, %1" :: "a"((uint32_t)data), "Nd"(port)); break;
        }
    }
#endif

    uacpi_status uacpi_kernel_raw_memory_read(uacpi_phys_addr address, uacpi_u8 byte_width, uacpi_u64* out_value)
    {
        VALIDATE_(out_value != nullptr, UACPI_STATUS_INVALID_ARGUMENT);
        VALIDATE_(byte_width <= 8 && sl::IsPowerOfTwo(byte_width), UACPI_STATUS_INVALID_ARGUMENT);

        const size_t hhdmLimit = npk_hhdm_limit();
        sl::NativePtr access = nullptr;

        if (address < hhdmLimit)
            access = address + npk_hhdm_base();
        else
        {
            //address is outside of hhdm, we'll need to temporarily map it
            access = npk_vm_alloc(byte_width, (void*)address, VmMmio, nullptr);
            if (access.ptr == nullptr)
                return UACPI_STATUS_INTERNAL_ERROR;
        }

        switch (byte_width)
        {
        case 1: *out_value = access.Read<uint8_t>(); break;
        case 2: *out_value = access.Read<uint16_t>(); break;
        case 4: *out_value = access.Read<uint32_t>(); break;
        case 8: *out_value = access.Read<uint64_t>(); break;
        }

        if (address >= hhdmLimit)
            npk_vm_free(access.ptr);

        return UACPI_STATUS_OK;
    }

    uacpi_status uacpi_kernel_raw_memory_write(uacpi_phys_addr address, uacpi_u8 byte_width, uacpi_u64 in_value)
    {
        VALIDATE_(byte_width <= 8 && sl::IsPowerOfTwo(byte_width), UACPI_STATUS_INVALID_ARGUMENT);

        const size_t hhdmLimit = npk_hhdm_limit();
        sl::NativePtr access = nullptr;

        if (address < hhdmLimit)
            access = address + npk_hhdm_base();
        else
        {
            //address is outside of hhdm, we'll need to temporarily map it
            const npk_vm_flags flags = (npk_vm_flags)(VmMmio | VmWrite);
            access = npk_vm_alloc(byte_width, (void*)address, flags, nullptr);
            if (access.ptr == nullptr)
                return UACPI_STATUS_INTERNAL_ERROR;
        }

        switch (byte_width)
        {
        case 1: access.Write<uint8_t>(in_value); break;
        case 2: access.Write<uint16_t>(in_value); break;
        case 4: access.Write<uint32_t>(in_value); break;
        case 8: access.Write<uint64_t>(in_value); break;
        }

        if (address >= hhdmLimit)
            npk_vm_free(access.ptr);

        return UACPI_STATUS_OK;
    }

    uacpi_status uacpi_kernel_raw_io_read(uacpi_io_addr address, uacpi_u8 byte_width, uacpi_u64* out_value)
    {
#ifdef __x86_64__
        VALIDATE_(address <= 0xFFFF, UACPI_STATUS_INVALID_ARGUMENT);
        VALIDATE_(byte_width <= 4 && byte_width != 3, UACPI_STATUS_INVALID_ARGUMENT);
        VALIDATE_(out_value != nullptr, UACPI_STATUS_INVALID_ARGUMENT);

        *out_value = PortRead(address, byte_width);
        return UACPI_STATUS_OK;
#else
        return UACPI_STATUS_UNIMPLEMENTED;
#endif
    }

    uacpi_status uacpi_kernel_raw_io_write(uacpi_io_addr address, uacpi_u8 byte_width, uacpi_u64 in_value)
    {
#ifdef __x86_64__
        VALIDATE_(address <= 0xFFFF, UACPI_STATUS_INVALID_ARGUMENT);
        VALIDATE_(byte_width <= 4 && byte_width != 3, UACPI_STATUS_INVALID_ARGUMENT);

        PortWrite(address, byte_width, in_value);
        return UACPI_STATUS_OK;
#else
        return UACPI_STATUS_UNIMPLEMENTED;
#endif
    }

    uacpi_status uacpi_kernel_pci_read(uacpi_pci_address* address, uacpi_size offset, uacpi_u8 byte_width, uacpi_u64* value)
    {
        (void)address; (void)offset; (void)byte_width; (void)value;

        Log("Attempted raw PCI read", LogLevel::Warning);
        return UACPI_STATUS_UNIMPLEMENTED;
    }

    uacpi_status uacpi_kernel_pci_write(uacpi_pci_address* address, uacpi_size offset, uacpi_u8 byte_width, uacpi_u64 value)
    {
        (void)address; (void)offset; (void)byte_width; (void)value;

        Log("Attempted raw PCI read", LogLevel::Warning);
        return UACPI_STATUS_UNIMPLEMENTED;
    }

    union IoMapping //:meme:
    {
        struct
        {
            uint16_t base;
            uint16_t length;
        };
        uintptr_t packed;
    };

    uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size len, uacpi_handle* out_handle)
    {
#ifdef __x86_64__
        VALIDATE_(base <= 0xFFFF, UACPI_STATUS_INVALID_ARGUMENT);
        VALIDATE_(len <= 0xFFFF, UACPI_STATUS_INVALID_ARGUMENT);

        IoMapping mapping { .base = (uint16_t)base, .length = (uint16_t)len };
        *out_handle = reinterpret_cast<uacpi_handle>(mapping.packed);

        return UACPI_STATUS_OK;
#else
        return UACPI_STATUS_UNIMPLEMENTED;
#endif
    }

    void uacpi_kernel_io_unmap(uacpi_handle handle)
    { (void)handle; } //no-op

    uacpi_status uacpi_kernel_io_read(uacpi_handle handle, uacpi_size offset, uacpi_u8 byte_width, uacpi_u64* value)
    {
#ifdef __x86_64__
        VALIDATE_(handle != nullptr, UACPI_STATUS_INVALID_ARGUMENT);
        VALIDATE_(offset <= 0xFFFF, UACPI_STATUS_INVALID_ARGUMENT);
        VALIDATE_(byte_width <= 4 && byte_width != 3, UACPI_STATUS_INVALID_ARGUMENT);
        VALIDATE_(value != nullptr, UACPI_STATUS_INVALID_ARGUMENT);

        const IoMapping mapping { .packed = reinterpret_cast<uintptr_t>(handle) };
        const uint16_t port = mapping.base + offset;

        *value = PortRead(port, byte_width);
        return UACPI_STATUS_OK;
#else
        return UACPI_STATUS_UNIMPLEMENTED;
#endif
    }

    uacpi_status uacpi_kernel_io_write(uacpi_handle handle, uacpi_size offset, uacpi_u8 byte_width, uacpi_u64 value)
    {
#ifdef __x86_64__
        VALIDATE_(handle != nullptr, UACPI_STATUS_INVALID_ARGUMENT);
        VALIDATE_(offset <= 0xFFFF, UACPI_STATUS_INVALID_ARGUMENT);
        VALIDATE_(byte_width <= 4 && byte_width != 3, UACPI_STATUS_INVALID_ARGUMENT);

        const IoMapping mapping { .packed = reinterpret_cast<uintptr_t>(handle) };
        const uint16_t port = mapping.base + offset;

        PortWrite(port, byte_width, value);
        return UACPI_STATUS_OK;
#else
        return UACPI_STATUS_UNIMPLEMENTED;
#endif
    }

    void* uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len)
    {
        if (len == 0)
            return nullptr;

        const npk_vm_flags flags = (npk_vm_flags)(VmMmio | VmWrite);
        return npk_vm_alloc(len, (void*)addr, flags, nullptr);
    }

    void uacpi_kernel_unmap(void *addr, uacpi_size len)
    {
        if (len == 0)
            return;

        npk_vm_free(addr);
    }
}
