#pragma once

#include <drivers/DriverManager.h>
#include <interfaces/driver/Io.h>
#include <tasking/Waitable.h>
#include <Atomic.h>
#include <Handle.h>

namespace Npk::Io
{
    enum class IopType
    {
        Read = 0,
        Write = 1,
    };

    struct IopFrame
    {
        npk_iop_frame apiFrame;
        sl::Handle<Drivers::DeviceApi> api;
    };

    struct IoPacket
    {
        sl::Atomic<size_t> references;
        sl::Vector<IopFrame> frames;
        npk_iop_context context;
        Tasking::Waitable completeEvent;
        size_t nextIndex;
        IopType type;
        int8_t directionMod; //+1 or -1 depending on direction through stack
        bool failure;
    };

    class IoManager
    {
    private:
    public:
        static IoManager& Global();

        void Init();

        sl::Handle<IoPacket> Begin(npk_iop_beginning* beginning);
        bool End(sl::Handle<IoPacket> iop);
        sl::Opt<bool> ContinueOne(sl::Handle<IoPacket> iop);
    };
}
