#pragma once

#include <Locks.h>
#include <containers/LinkedList.h>
#include <containers/Vector.h>
#include <devices/GenericDevices.h>
#include <Optional.h>
#include <Atomic.h>

namespace Npk::Devices
{
    class DeviceManager
    {
    private:
        sl::TicketLock listLock;
        sl::LinkedList<GenericDevice*> devices;
        sl::Atomic<size_t> nextId;

    public:
        static DeviceManager& Global();

        void Init();

        sl::Opt<size_t> AttachDevice(GenericDevice* instance, bool delayInit = false);
        sl::Opt<GenericDevice*> DetachDevice(size_t id, bool force = false);

        sl::Opt<GenericDevice*> GetDevice(size_t id);
        sl::Vector<GenericDevice*> GetDevices(DeviceType type);
    };
}
