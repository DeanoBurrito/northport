#pragma once

#include <containers/Vector.h>
#include <config/DeviceTree.h>
#include <devices/PciAddress.h>
#include <Locks.h>

namespace Npk::Devices
{
    enum class PciSpaceType : uint8_t
    {
        //these correspond the type encodings used in device
        //tree 'ranges' properties used by pcie root complexes.
        Config = 0b00,
        BarIo = 0b01,
        Bar32 = 0b10,
        Bar64 = 0b11,

        Dma, //means translation is how PCI views physical memory
    };

    struct PciSpaceMap
    {
        PciSpaceType type;
        uintptr_t physBase;
        uintptr_t pciBase;
        size_t length;

        size_t allocated;

        PciSpaceMap(PciSpaceType t, uintptr_t phys, uintptr_t pciAddr, size_t len)
        : type(t), physBase(phys), pciBase(pciAddr), length(len), allocated(0)
        {}
    };
    
    class PciBridge
    {
    private:
        sl::Vector<PciSpaceMap> maps;
        sl::Vector<PciAddress> addresses;
        sl::TicketLock lock;

        void ScanSegment(uintptr_t segmentBase, uint8_t startBus, uint16_t segId, bool ecamAvailable);
        bool ParseTranslations(const Config::DtNode& node, const char* propName, bool outbound);
        uintptr_t AllocForBar(PciSpaceType type, size_t length);
        void AllocateBars();
        void DetectDrivers();

    public:
        constexpr PciBridge() : addresses(), lock()
        {}

        static PciBridge& Global();

        void Init();
        sl::Opt<uintptr_t> PciToHost(uintptr_t addr, PciSpaceType type);
        sl::Opt<uintptr_t> HostToPci(uintptr_t addr);
    };
}
