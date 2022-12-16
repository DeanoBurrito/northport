#pragma once

#include <devices/PciAddress.h>

namespace Npk::Devices
{
    constexpr uint8_t PciCapMsi = 0x5;
    constexpr uint8_t PciCapVendor = 0x9;
    constexpr uint8_t PciCapMsix = 0x11;

    struct PciCap
    {
        const PciAddress base;
        const size_t offset;
        
        PciCap(PciAddress base, size_t offset) : base(base), offset(offset)
        {}

        static sl::Opt<PciCap> Find(PciAddress addr, uint8_t id, sl::Opt<PciCap> start = {});

        uint8_t Id() const;
        uint32_t ReadReg(size_t index) const;
        void WriteReg(size_t index, uint32_t value) const;
        bool BitReadWrite(size_t regIndex, size_t bitIndex, sl::Opt<bool> setValue = {}) const;
    };

    struct MsiCap
    {
        PciCap cap;

        MsiCap(PciCap cap) : cap(cap)
        {}

        [[gnu::always_inline]]
        inline bool Enabled(sl::Opt<bool> set = {})
        { return cap.BitReadWrite(1, 16, set); }

        void SetMessage(uintptr_t addr, uint16_t data) const;
    };

    struct MsixCap
    {
        PciCap cap;

        MsixCap(PciCap cap) : cap(cap)
        {}

        [[gnu::always_inline]]
        inline bool Enable(sl::Opt<bool> set = {}) const
        { return cap.BitReadWrite(0, 31, set); }

        [[gnu::always_inline]]
        inline bool GlobalMask(sl::Opt<bool> set = {}) const
        { return cap.BitReadWrite(0, 30, set); }

        size_t TableSize() const;
        void SetEntry(size_t index, uintptr_t addr, uint32_t data, bool masked) const;
        void MaskEntry(size_t index, bool mask) const;
    };
}
