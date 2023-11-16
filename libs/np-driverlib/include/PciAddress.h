#pragma once

#include <stdint.h>
#include <Optional.h>

namespace dl
{
    struct PciBar
    {
        uintptr_t base;
        size_t length;
        uint8_t index;
        bool isPrefetchable;
        bool is64bit;
        bool isMemory;
    };

    class PciAddress
    {
    private:
        uintptr_t addr;

    public:
        constexpr PciAddress() : addr(0)
        {}

        constexpr PciAddress(uintptr_t addr) : addr(addr)
        {}

        static PciAddress FromEcam(uintptr_t segmentBase, uint8_t bus, uint8_t device, uint8_t func);
        static PciAddress FromLegacy(uint8_t bus, uint8_t device, uint8_t func);

        void Write(size_t offset, uint32_t value) const;
        uint32_t Read(size_t offset) const;
        PciBar ReadBar(uint8_t index, bool noSize = false) const;
        bool BitRw(size_t reg, size_t index, sl::Opt<bool> set = {}) const;

        inline void WriteReg(size_t index, uint32_t value) const
        { Write(index * 4, value); }

        inline uint32_t ReadReg(size_t index) const
        { return Read(index * 4); }

        [[gnu::always_inline]]
        inline bool BusMastering(sl::Opt<bool> set = {}) const
        { return BitRw(1, 2, set); }

        [[gnu::always_inline]]
        inline bool InterruptDisable(sl::Opt<bool> set = {}) const
        { return BitRw(1, 10, set); }

        [[gnu::always_inline]]
        inline bool MemorySpaceEnable(sl::Opt<bool> set = {}) const
        { return BitRw(1, 1, set); }

        [[gnu::always_inline]]
        inline bool InterruptPending() const
        { return BitRw(1, 19, {}); }

        [[gnu::always_inline]]
        inline bool HasCapsList() const
        { return BitRw(1, 20, {}); }
    };
}
