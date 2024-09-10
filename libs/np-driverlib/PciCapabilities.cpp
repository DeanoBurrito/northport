#include <PciCapabilities.h>
#include <interfaces/driver/Memory.h>
#include <Log.h>
#include <NativePtr.h>
#include <Maths.h>

namespace dl
{
    MsiCapability::MsiCapability(PciAddress address, uint8_t ptr)
    {
        addr = address;
        configPtr = ptr;

        const uint32_t header = addr.Read(ptr);
        msix = (header & 0xFF) == (uint8_t)PciCapabilityType::Msix;

        if (msix)
        {
            const uint32_t tableReg = addr.Read(ptr + 4);
            const uint32_t pbaReg = addr.Read(ptr + 8);
            const uintptr_t tablePhysAddr = addr.ReadBar(tableReg & 7, true).base + (tableReg & ~7);
            const uintptr_t pbaPhysAddr = addr.ReadBar(pbaReg & 7, true).base + (pbaReg & ~7);
            const size_t vectors = ((header >> 16) & 0x3FF) + 1;

            const npk_vm_flags vmFlags = (npk_vm_flags)(VmWrite | VmMmio);
            msixTable = npk_vm_alloc(vectors * 16, reinterpret_cast<void*>(tablePhysAddr), vmFlags, nullptr);
            msixPba = npk_vm_alloc(sl::AlignUp(vectors, 8) / 8, reinterpret_cast<void*>(pbaPhysAddr), vmFlags, nullptr);
            VALIDATE(msixTable != nullptr,, "Failed to map MSI-X vector table");
            VALIDATE(msixPba != nullptr,, "Failed to map MSI-X PBA table");
        }
        else
        {
            msi64BitAddr = (header >> 23) & 1;
            msiMasking = (header >> 24) & 1;
        }
    }

    MsiCapability::~MsiCapability()
    {
        if (msix)
        {
            npk_vm_free(msixTable);
            npk_vm_free(msixPba);
        }
    }

    void MsiCapability::Enable(bool yes) const
    {
        if (!yes)
        {
            //disable capability
            uint32_t header = addr.Read(configPtr);
            if (msix)
                header &= ~(1 << 31);
            else
                header &= ~(1 << 16);
            addr.Write(configPtr, header);
            return;
        }

        //enable capability
        if (msix)
        {
            uint32_t header = addr.Read(configPtr);
            header |= 1 << 31; //set enable bit
            header &= ~(1 << 30); //disable global mask
            addr.Write(configPtr, header);
        }
        else
        {
            uint32_t header = addr.Read(configPtr);
            header |= 1 << 16; //set enable bit
            header &= ~(3 << 20); //clear multiple message enable, meaning only 1 vector allocated
            addr.Write(configPtr, header);
        }
    }

    size_t MsiCapability::VectorCount() const
    {
        if (msix)
            return ((addr.Read(configPtr) >> 16) & 0x3FF) + 1;
        else
            return 1;
    }

    bool MsiCapability::SetVector(size_t index, uintptr_t address, uintptr_t message, bool masked) const
    {
        if (index >= VectorCount())
            return false;

        if (msix)
        {
            sl::NativePtr entry = reinterpret_cast<uintptr_t>(msixTable) + (index * 16);
            entry.Write<uint32_t>(address & 0xFFFF'FFFF);
#if __SIZEOF_POINTER__ >= 8
            entry.Offset(4).Write<uint32_t>(address >> 32);
#endif
            entry.Offset(8).Write<uint32_t>(message & 0xFFFF'FFFF);
            entry.Offset(12).Write<uint32_t>(masked ? 1 : 0);
        }
        else
        {
            uint8_t maskOffset = 0xC;
            addr.Write(configPtr + 4, address);
            if (msi64BitAddr)
            {
#if __SIZEOF_POINTER__ >= 8
                addr.Write(configPtr + 8, address >> 32);
#endif
                addr.Write(configPtr + 0xC, message);
                maskOffset = 0x10;
            }
            else
                addr.Write(configPtr + 8, message);

            if (msiMasking)
                addr.Write(configPtr + maskOffset, masked ? 1 : 0);
        }
        return true;
    }

    bool MsiCapability::Mask(size_t index, bool masked) const
    {
        if (index >= VectorCount())
            return false;

        if (msix)
        {
            sl::NativePtr entry = reinterpret_cast<uintptr_t>(msixTable) + (index * 16);
            entry.Offset(12).Write<uint32_t>(masked ? 1 : 0);
        }
        else
        {
            if (!msiMasking)
                return false;
            const uint8_t maskOffset = 0xC + (msi64BitAddr ? 4 : 0);
            addr.Write(configPtr + maskOffset, masked ? 1 : 0);
        }
        return true;
    }

    bool MsiCapability::IsMasked(size_t index) const
    {
        if (index >= VectorCount())
            return true;

        if (msix)
        {
            sl::NativePtr entry = reinterpret_cast<uintptr_t>(msixTable) + (index * 16);
            return entry.Offset(12).Read<uint32_t>() & 1;
        }

        if (!msiMasking)
            return false;
        const uint8_t maskOffset = 0xC + (msi64BitAddr ? 4 : 0);
        return addr.Read(configPtr + maskOffset) & 1;
    }

    bool MsiCapability::IsPending(size_t index) const
    {
        if (index >= VectorCount())
            return false;

        if (msix)
        {
            if (msixPba == nullptr)
                return false;

            const size_t byteOffset = index / 8;
            const size_t bitOffset = index % 8;
            return static_cast<volatile uint8_t*>(msixPba)[byteOffset] & (1 << bitOffset);
        }

        if (!msiMasking)
            return false;
        const uint8_t pendingOffset = 0x10 + (msi64BitAddr ? 4 : 0);
        return addr.Read(configPtr + pendingOffset) & 1;
    }

    sl::Opt<uint8_t> FindPciCapability(PciAddress addr, PciCapabilityType type, size_t startOffset)
    {
        //check pci function has a caps list at all
        const uint16_t status = addr.ReadReg(1) >> 16;
        if ((status & (1 << 4)) == 0)
            return {};

        uint8_t scanPtr = addr.Read(0x34) & 0xFF;
        if (startOffset != 0)
            scanPtr = (addr.Read(startOffset) >> 8) & 0xFF;
        while (scanPtr != 0)
        {
            const uint32_t capReg = addr.Read(scanPtr);
            if ((capReg & 0xFF) == (uint8_t)type)
                return scanPtr;
            scanPtr = (capReg >> 8) & 0xFF;
        }

        return {};
    }

    sl::Opt<MsiCapability> FindMsi(PciAddress addr)
    {
        //prefer msix where possible, but allow for using msi.
        const auto msix = FindPciCapability(addr, PciCapabilityType::Msix);
        if (msix.HasValue())
            return MsiCapability(addr, *msix);
        const auto msi = FindPciCapability(addr, PciCapabilityType::Msi);
        if (msix.HasValue())
            return MsiCapability(addr, *msi);
        return {};
    }
}
