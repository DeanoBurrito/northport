#include <io/IoManager.h>
#include <debug/Log.h>

namespace Npk::Io
{
    IoManager globalIoManager;

    IoManager& IoManager::Global()
    { return globalIoManager; }

    void IoManager::Init()
    {
        Log("IO manager initialized.", LogLevel::Info);
    }

    sl::Handle<IoPacket> IoManager::Begin(npk_iop_beginning* beginning)
    {
        VALIDATE_(beginning != nullptr, {});

        using namespace Drivers;
        auto deviceStack = DriverManager::Global().GetStackFromId(beginning->device_api_id);
        VALIDATE_(deviceStack.Size() > 0, {});

        //acquire handles to the device APIs now so that there's no exciting
        //state changes while running the iop.
        sl::Vector<IopFrame> frames(deviceStack.Size());
        sl::Handle<DeviceDescriptor> nextDescriptor {};
        for (size_t i = 0; i < deviceStack.Size(); i++)
        {
            VALIDATE_(deviceStack[i].Valid(), {});
            
            if (deviceStack[i]->type == DeviceNodeType::Descriptor)
                nextDescriptor = static_cast<DeviceDescriptor*>(*deviceStack[i]);
            if (deviceStack[i]->type != DeviceNodeType::DriverInstance)
                continue;

            auto* driver = static_cast<DriverInstance*>(*deviceStack[i]);
            if (!driver->transportDevice.Valid())
                break;

            auto& frame = frames.EmplaceBack();
            frame.api = driver->transportDevice;

            if (nextDescriptor.Valid())
            {
                frame.apiFrame.descriptor_data = nextDescriptor->apiDesc->driver_data;
                frame.apiFrame.descriptor_id = nextDescriptor->id;
                nextDescriptor = nullptr;
            }
        }

        //prime the first frame with the buffers provided.
        frames[0].apiFrame.buffer = beginning->buffer;
        frames[0].apiFrame.addr = beginning->addr;
        frames[0].apiFrame.length = beginning->length;

        IoPacket* iop = new IoPacket();
        iop->frames = sl::Move(frames);
        iop->type = static_cast<IopType>(beginning->type);
        iop->directionMod = +1;
        iop->nextIndex = 0;
        iop->failure = false;
        iop->context.op_type = beginning->type;
        return iop;
    }

    bool IoManager::End(sl::Handle<IoPacket> iop)
    {
        VALIDATE_(iop.Valid(), false);

        sl::Opt<bool> result {};
        while (!result.HasValue())
            result = ContinueOne(iop);

        return *result;
    }

    sl::Opt<bool> IoManager::ContinueOne(sl::Handle<IoPacket> iop)
    {
        VALIDATE_(iop.Valid(), false);

        //TODO: run any dependant IOPs before continuing with this one
        //TODO: carry buffers between frames, or allow them to be overriden.

        auto* deviceApi = iop->frames[iop->nextIndex].api->api;
        auto* ioApi = reinterpret_cast<const npk_io_device_api*>(deviceApi);

        if (iop->directionMod > 0)
        {
            if (!ioApi->begin_op(deviceApi, &iop->context, &iop->frames[iop->nextIndex].apiFrame))
            {
                iop->directionMod = -1;
                iop->failure = true;
            }
        }
        else
        {
            if (!ioApi->end_op(deviceApi, &iop->context, &iop->frames[iop->nextIndex].apiFrame))
                iop->failure = true;
        }

        if (iop->directionMod == -1 && iop->nextIndex == 0)
        {
            //operation has completed
            iop->completeEvent.Signal();
            return !iop->failure;
        }

        if (iop->nextIndex == iop->frames.Size() - 1)
            iop->directionMod = -1;
        else
            iop->nextIndex += iop->directionMod;
        return {}; //not completed yet, so we cant return a success/failure code.
    }
}
