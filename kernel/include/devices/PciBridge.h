#pragma once

#include <Locks.h>

namespace Npk::Devices
{
    struct PciAddr
    {
        uintptr_t segmentBase;
        uint8_t bus;
        uint8_t device;
        uint8_t function;
    };

    class PciBridge
    {
    private:
        sl::TicketLock lock;

        void ScanSegment(uintptr_t segmentBase, uint8_t startBus, uint16_t segId, bool ecamAvailable);
        bool TryInitFromAcpi();
        bool TryInitFromDtb();

    public:
        constexpr PciBridge() :  lock()
        {}
        static PciBridge& Global();

        void Init();
    };
}
