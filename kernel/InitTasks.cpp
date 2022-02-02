#include <Log.h>
#include <scheduling/Scheduler.h>
#include <devices/Ps2Controller.h>
#include <devices/PciBridge.h>
#include <drivers/DriverManager.h>
#include <devices/DeviceManager.h>

//TODO: move graphics init into device manager (init primary devices?)
#include <LinearFramebuffer.h>
#include <TerminalFramebuffer.h>

using namespace Kernel::Scheduling;

void InitDisplayAdaptorTask()
{
    //just draw a test pattern for now, not much to initialize
    np::Graphics::LinearFramebuffer::Screen()->DrawTestPattern();

    np::Graphics::TerminalFramebuffer fb(np::Graphics::LinearFramebuffer::Screen());
    fb.Print("Hello world!", {0, 0});
}

void InitPs2Task()
{
    using namespace Kernel::Devices;
    
    size_t ps2PortCount = Ps2Controller::InitController();
    if (ps2PortCount > 0)
        Ps2Controller::Keyboard()->Init(false);
    if (ps2PortCount > 1)
        Ps2Controller::Mouse()->Init(true);
}

void InitPciTask()
{
    Kernel::Devices::PciBridge::Global()->Init();

    Scheduler::Global()->CreateThread((size_t)InitDisplayAdaptorTask, ThreadFlags::KernelMode)->Start(nullptr);
}

void InitManagersTask()
{
    Kernel::Devices::DeviceManager::Global()->Init();
    Kernel::Drivers::DriverManager::Global()->Init();

    Scheduler::Global()->CreateThread((size_t)InitPciTask, ThreadFlags::KernelMode)->Start(nullptr);
}

extern "C" void QueueInitTasks()
{
    Scheduler::Global()->CreateThread((size_t)InitPs2Task, ThreadFlags::KernelMode)->Start(nullptr);
    Scheduler::Global()->CreateThread((size_t)InitManagersTask, ThreadFlags::KernelMode)->Start(nullptr);
}
