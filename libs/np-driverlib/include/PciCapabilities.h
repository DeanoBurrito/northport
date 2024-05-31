#pragma once

#include <PciAddress.h>
#include <Optional.h>

namespace dl
{
    enum class PciCapabilityType : uint8_t
    {
        Reserved = 0x0,
        Msi = 0x5,
        Vendor = 0x9,
        Pcie = 0x10,
        Msix = 0x11,
    };

    //dont be decieved by the name 'MsiCapability', this class abstracts both MSI and MSI-X interfaces.
    //using `FindMsi()` will return an instance of this, driven by whichever interface it finds (preferring MSI-X).
    class MsiCapability
    {
    private:
        PciAddress addr;
        uint8_t configPtr;
        bool msix;
        bool msi64BitAddr;
        bool msiMasking;
        void* msixTable;
        void* msixPba;

    public:
        MsiCapability() : configPtr(0)
        {}

        MsiCapability(PciAddress addr, uint8_t ptr);
        ~MsiCapability();

        inline bool IsMsix() const
        { return msix; }

        void Enable(bool yes) const;
        size_t VectorCount() const;
        bool SetVector(size_t index, uintptr_t address, uintptr_t message, bool masked) const;
        bool Mask(size_t index, bool masked) const;
        bool IsMasked(size_t index) const;
        bool IsPending(size_t index) const;
    };

    sl::Opt<uint8_t> FindPciCapability(PciAddress addr, PciCapabilityType type, size_t startOffset = 0);
    sl::Opt<MsiCapability> FindMsi(PciAddress addr);
}
