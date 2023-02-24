#include <debug/TerminalDriver.h>
#include <debug/Terminal.h>
#include <debug/Log.h>

namespace Npk::Debug
{
    Terminal kernelTerminal;

    void TerminalWriteCallback(const char* str, size_t len)
    {
        kernelTerminal.Write(str, len);
    }

    void InitEarlyTerminal()
    {
        if (kernelTerminal.Init(DefaultTerminalStyle))
            AddEarlyLogOutput(TerminalWriteCallback);
        else
            kernelTerminal.Deinit();
    }

    bool TerminalSerialDriver::Init()
    {
        sl::ScopedLock scopeLock(lock);
        status = Devices::DeviceStatus::Starting;
        
        //if terminal is already initialized (as an early output), this does nothing.
        kernelTerminal.Init(DefaultTerminalStyle);

        //TODO: we should listen to updates about the primary framebuffer
        status = Devices::DeviceStatus::Online;
        return true;
    }

    bool TerminalSerialDriver::Deinit()
    {
        ASSERT_UNREACHABLE();
    }

    void TerminalSerialDriver::Write(sl::Span<uint8_t> buffer)
    {
        sl::ScopedLock scopeLock(lock);
        kernelTerminal.Write(reinterpret_cast<char*>(buffer.Begin()), buffer.Size());
    }

    size_t TerminalSerialDriver::Read(sl::Span<uint8_t> buffer)
    {
        (void)buffer;
        return 0;
    }

    bool TerminalSerialDriver::InputAvailable()
    {
        return false;
    }
}
