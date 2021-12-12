#pragma once

#include <NativePtr.h>
#include <Platform.h>
#include <containers/Vector.h>

namespace Kernel::Devices
{
    constexpr static inline uint64_t IoApicRegisterSelectOffset = 0;
    constexpr static inline uint64_t IoApicRegisterWindowOffset = 0x10;
    
    enum class IoApicRegister : uint64_t
    {
        //bits 24-27 contains the 4bit id
        Id = 0x0,
        //bits 16-23 contain 8bit number of max redirect entries minos one, bits0-7 contain the implemenation version
        Version = 0x1,
        //bits 24-27 contain the arbitration id
        ArbitrationId = 0x2,

        Redirect0 = 0x10,
        Redirect1 = 0x12,
        Redirect2 = 0x14,
        Redirect4 = 0x16,
        Redirect5 = 0x18,
        Redirect6 = 0x1A,
        Redirect7 = 0x1C,
        Redirect8 = 0x1E,
        Redirect9 = 0x20,
        Redirect10 = 0x22,
        Redirect11 = 0x24,
        Redirect12 = 0x26,
        Redirect13 = 0x28,
        Redirect14 = 0x2A,
        Redirect15 = 0x2C,
        Redirect16 = 0x2E,
        Redirect17 = 0x30,
        Redirect18 = 0x32,
        Redirect19 = 0x34,
        Redirect20 = 0x36,
        Redirect21 = 0x38,
        Redirect22 = 0x3A,
        Redirect23 = 0x3C,
    };

    enum class IoApicTriggerMode : size_t
    {
        Edge = 0,
        Level = 1,
    };

    enum class IoApicPinPolarity : size_t
    {
        ActiveHigh = 0,
        ActiveLow = 1,
    };

    //this is basically an lvt, nicely done intel.
    struct IoApicRedirectEntry
    {
        uint64_t raw;

        //NOTE: this is destructive and ignores previous register value
        void Set(uint8_t destApicId, uint8_t vector, IoApicTriggerMode triggerMode = IoApicTriggerMode::Edge, IoApicPinPolarity polarity = IoApicPinPolarity::ActiveHigh);
        void SetMask(bool masked = true);
        bool IsMasked();
        FORCE_INLINE void ClearMask()
        { SetMask(false); }
    };
    
    class IoApic
    {
    private:
        size_t gsiBase;
        uint8_t apicId;
        sl::NativePtr baseAddress;
        size_t maxRedirects;

        void WriteReg(IoApicRegister reg, uint32_t value);
        uint32_t ReadReg(IoApicRegister reg);

        void Init(uint64_t address, uint8_t id, uint8_t gsiBase);

    public:
        //create ioapic for all available ioapics
        static void InitAll();
        static IoApic* Global(size_t ownsGsi = 0);

        void WriteRedirect(uint8_t pinNum, IoApicRedirectEntry entry);
        IoApicRedirectEntry ReadRedirect(uint8_t pinNum);
    };
}
