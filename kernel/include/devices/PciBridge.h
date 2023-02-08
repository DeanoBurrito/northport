#pragma once

#include <containers/Vector.h>
#include <devices/PciAddress.h>
#include <Locks.h>

namespace Npk::Devices
{
    class PciBridge
    {
    private:
        sl::Vector<PciAddress> addresses;
        sl::TicketLock lock;

        void ScanSegment(uintptr_t segmentBase, uint8_t startBus, uint16_t segId, bool ecamAvailable);

    public:
        constexpr PciBridge() : addresses(), lock()
        {}

        static PciBridge& Global();

        void Init();
    };
}
