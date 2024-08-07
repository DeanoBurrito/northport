#include <debug/Log.h>
#include <interfaces/driver/Io.h>
#include <interfaces/Helpers.h>
#include <io/IoManager.h>

extern "C"
{
    using namespace Npk::Io;

    DRIVER_API_FUNC
    npk_handle npk_begin_iop(REQUIRED npk_iop_beginning* begin)
    {
        VALIDATE_(begin != nullptr, NPK_INVALID_HANDLE);

        sl::Handle<IoPacket> iop = IoManager::Global().Begin(begin);
        VALIDATE_(iop.Valid(), NPK_INVALID_HANDLE);

        //TODO: handle table
        iop->references++;
        return reinterpret_cast<npk_handle>(*iop);
    }

    DRIVER_API_FUNC
    bool npk_end_iop(npk_handle iop)
    {
        sl::Handle<IoPacket> handle = reinterpret_cast<IoPacket*>(iop);
        const bool success = IoManager::Global().End(handle);
        handle->references--;
        return success;
    }
}
