#include <Log.h>
#include <scheduling/Scheduler.h>
#include <scheduling/Thread.h>
#include <devices/ps2/Ps2Driver.h>
#include <devices/PciBridge.h>
#include <drivers/DriverManager.h>
#include <devices/DeviceManager.h>
#include <filesystem/Vfs.h>
#include <Configuration.h>
#include <Loader.h>

using namespace Kernel::Scheduling;
ThreadGroup* initTaskThreadGroup;

void InitUserspaceTask()
{
    //TODO: we should wait until all the main kernel tasks have finished running before starting this. Look into the main thread group exiting?
    //something of flag will need to be set. Alternatively we could use a callback for when the init thread group gets cleaned up?
    using namespace Kernel::Filesystem;
    auto maybeStartupFile = VFS::Global()->FindNode("/initdisk/apps/startup.elf");
    if (!maybeStartupFile)
    {
        Log("Couldnt find startup.elf, kernel will finish init and then halt.", Kernel::LogSeverity::Warning);
        return;
    }

    auto maybeThreadId = Kernel::LoadElfFromFile("/initdisk/apps/startup.elf", ThreadFlags::None);
    if (!maybeThreadId)
    {
        Log("Couldnt load startup.elf, check error log.", Kernel::LogSeverity::Warning);
        return;
    }

    Thread* startupThread = Scheduler::Global()->GetThread(*maybeThreadId).Value();
    // startupThread->Start(nullptr);
    
    auto maybeWindowid = Kernel::LoadElfFromFile("/initdisk/apps/server-window.elf", ThreadFlags::None);
    if (!maybeWindowid)
    {
        Log("Couldnt load server-window.elf, check error log.", Kernel::LogSeverity::Warning);
        return;
    }

    Kernel::LogEnableDest(Kernel::LogDest::FramebufferOverwrite, false);
    Thread* serverStartupThread = Scheduler::Global()->GetThread(*maybeWindowid).Value();
    serverStartupThread->Start(nullptr);

    Thread::Current()->Exit();
}

void InitPs2Task()
{
    using namespace Kernel::Devices;
    using namespace Kernel::Drivers;
    
    if (!Ps2::Ps2Driver::Available())
    {
        Log("PS/2 is not supported on this system, driver will not be loaded.", Kernel::LogSeverity::Info);
        return;
    }

    uint8_t ps2MachineName[] = { "x86ps2" };
    auto maybeManifest = DriverManager::Global()->FindDriver(DriverSubsystem::None, {6, ps2MachineName});
    if (!maybeManifest)
    {
        Log("Could not find ps/2 peripherals driver.", Kernel::LogSeverity::Warning);
        return;
    }

    DriverManager::Global()->StartDriver(*maybeManifest, nullptr);
    Thread::Current()->Exit();
}

void InitPciTask()
{
    Kernel::Devices::PciBridge::Global()->Init();
    Scheduler::Global()->CreateThread((size_t)InitUserspaceTask, ThreadFlags::KernelMode, initTaskThreadGroup)->Start(nullptr);
    Thread::Current()->Exit();
}

void InitManagersTask()
{
    using namespace Kernel::Devices;
    using namespace Kernel::Drivers;
    
    DeviceManager::Global()->Init();
    DriverManager::Global()->Init();

    //start initdisk driver
    uint8_t initDiskMachineName[] = { "initdisk" };
    auto maybeDriver = DriverManager::Global()->FindDriver(DriverSubsystem::Filesystem, { 8, initDiskMachineName });
    if (maybeDriver)
        DriverManager::Global()->StartDriver(*maybeDriver, nullptr);
    else //we should never not have this, its baked into the kernel.
        Log("Initdisk filesystem driver is not available!", Kernel::LogSeverity::Error);
    
    //try load config file from initdisk, and parse it into configuration
    auto maybeConfigFile = Kernel::Filesystem::VFS::Global()->FindNode("/initdisk/config/kernel.cfg");
    if (maybeConfigFile)
    {
        auto file = *maybeConfigFile;
        uint8_t* buffer = new uint8_t[file->Details().filesize];
        if (file->Read(0, buffer, 0, file->Details().filesize) == file->Details().filesize)
            Kernel::Configuration::Global()->SetMany((const char*)buffer);
        
        delete[] buffer;
    }
    else
        Log("Could not load extra configuration from /initdisk/config/kernel.cfg: file not found.", Kernel::LogSeverity::Warning);

    Scheduler::Global()->CreateThread((size_t)InitPs2Task, ThreadFlags::KernelMode, initTaskThreadGroup)->Start(nullptr);
    Scheduler::Global()->CreateThread((size_t)InitPciTask, ThreadFlags::KernelMode, initTaskThreadGroup)->Start(nullptr);
    Thread::Current()->Exit();
}

void QueueInitTasks()
{
    initTaskThreadGroup = Scheduler::Global()->CreateThreadGroup();
    initTaskThreadGroup->Name() = "InitTasks";

    Scheduler::Global()->CreateThread((size_t)InitManagersTask, ThreadFlags::KernelMode, initTaskThreadGroup)->Start(nullptr);
}
