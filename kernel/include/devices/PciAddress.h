#pragma once

#include <stdint.h>
#include <stddef.h>
#include <Optional.h>

namespace Npk::Devices
{
    struct PciBar
    {
        uintptr_t address;
        size_t size;
        uint8_t index;
        bool isPrefetchable;
        bool is64Bit;
        bool isMemory;
    };

    enum class PciReg : size_t
    {
        Id = 0,
        CmdStatus = 1,
        Class = 2,
        LatencyCache = 3,
        Bar0 = 4,
        Bar1 = 5,
        Bar2 = 6,
        Bar3 = 7,
        Bar4 = 8,
        Bar5 = 9,
        CisPtr = 10,
        SubsystemId = 11,
        ExRom = 12,
        CapsPtr = 13,
        Interrupts = 15
    };

    struct PciAddress
    {
    private:
        uintptr_t addr;

    public:
        constexpr PciAddress() : addr(0) 
        {}

        constexpr PciAddress(uintptr_t address) : addr(address)
        {}

        //TODO: dtor needs to free allocated window from VMM!
        static PciAddress CreateEcam(uintptr_t segmentBase, uint8_t bus, uint8_t device, uint8_t function);
        static PciAddress CreateLegacy(uint8_t bus, uint8_t device, uint8_t function);

        void WriteAt(size_t offset, uint32_t value) const;
        uint32_t ReadAt(size_t offset) const;

        inline void WriteReg(PciReg reg, uint32_t value) const
        { WriteAt((size_t)reg * 4, value); }

        inline uint32_t ReadReg(PciReg reg) const
        { return ReadAt((size_t)reg * 4); }

        bool IsLegacy() const;
        PciBar ReadBar(size_t index, bool noSize = false) const;

        //returns the state of a bit, optionally writes to it.
        bool BitReadWrite(PciReg reg, size_t index, sl::Opt<bool> setValue = {}) const;

        [[gnu::always_inline]]
        inline bool BusMastering(sl::Opt<bool> set = {}) const
        { return BitReadWrite(PciReg::CmdStatus, 2, set); }

        [[gnu::always_inline]]
        inline bool InterruptDisable(sl::Opt<bool> set = {}) const
        { return BitReadWrite(PciReg::CmdStatus, 10, set); }

        [[gnu::always_inline]]
        inline bool MemorySpaceEnable(sl::Opt<bool> set = {}) const
        { return BitReadWrite(PciReg::CmdStatus, 1, set); }

        [[gnu::always_inline]]
        inline bool InterruptPending() const
        { return BitReadWrite(PciReg::CmdStatus, 19, {}); }

        [[gnu::always_inline]]
        inline bool HasCapsList() const
        { return BitReadWrite(PciReg::CmdStatus, 20, {}); }
    };
}
