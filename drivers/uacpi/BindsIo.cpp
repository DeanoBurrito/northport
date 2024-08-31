#include <uacpi/kernel_api.h>
#include <interfaces/driver/Memory.h>
#include <interfaces/driver/Devices.h>
#include <Log.h>
#include <NativePtr.h>
#include <Maths.h>

extern "C"
{
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
        return npk_access_bus(BusPortIo, byte_width, address, out_value, false)
            ? UACPI_STATUS_OK : UACPI_STATUS_INTERNAL_ERROR;
    }

    uacpi_status uacpi_kernel_raw_io_write(uacpi_io_addr address, uacpi_u8 byte_width, uacpi_u64 in_value)
    {
        return npk_access_bus(BusPortIo, byte_width, address, &in_value, true)
            ? UACPI_STATUS_OK : UACPI_STATUS_INTERNAL_ERROR;
    }

    uacpi_status uacpi_kernel_pci_read(uacpi_pci_address* addr, uacpi_size offset, uacpi_u8 byte_width, uacpi_u64* value)
    {
        const uintptr_t busAddr = NPK_MAKE_PCI_BUS_ADDR(addr->segment, addr->bus, addr->device, addr->function, offset);

        return npk_access_bus(BusPci, byte_width, busAddr, value, false)
            ? UACPI_STATUS_OK : UACPI_STATUS_INTERNAL_ERROR;
    }

    uacpi_status uacpi_kernel_pci_write(uacpi_pci_address* addr, uacpi_size offset, uacpi_u8 byte_width, uacpi_u64 value)
    {
        const uintptr_t busAddr = NPK_MAKE_PCI_BUS_ADDR(addr->segment, addr->bus, addr->device, addr->function, offset);

        return npk_access_bus(BusPci, byte_width, busAddr, &value, true)
            ? UACPI_STATUS_OK : UACPI_STATUS_INTERNAL_ERROR;
    }

    uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size len, uacpi_handle* out_handle)
    {
        (void)len;

        *out_handle = reinterpret_cast<uacpi_handle>(base);
        return UACPI_STATUS_OK;
    }

    void uacpi_kernel_io_unmap(uacpi_handle handle)
    { (void)handle; } //no-op

    uacpi_status uacpi_kernel_io_read(uacpi_handle handle, uacpi_size offset, uacpi_u8 byte_width, uacpi_u64* value)
    {
        auto base = reinterpret_cast<uacpi_io_addr>(handle);
        return npk_access_bus(BusPortIo, byte_width, base + offset, value, false)
            ? UACPI_STATUS_OK : UACPI_STATUS_INTERNAL_ERROR;
    }

    uacpi_status uacpi_kernel_io_write(uacpi_handle handle, uacpi_size offset, uacpi_u8 byte_width, uacpi_u64 value)
    {
        auto base = reinterpret_cast<uacpi_io_addr>(handle);
        return npk_access_bus(BusPortIo, byte_width, base + offset, &value, false)
            ? UACPI_STATUS_OK : UACPI_STATUS_INTERNAL_ERROR;
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
