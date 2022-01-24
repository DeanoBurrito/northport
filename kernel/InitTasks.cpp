#include <Log.h>
#include <scheduling/Scheduler.h>
#include <devices/Ps2Controller.h>
#include <devices/PciBridge.h>
#include <drivers/DriverManager.h>

void InitPs2Task()
{
    using namespace Kernel::Devices;
    
    size_t ps2PortCount = Ps2Controller::InitController();
    if (ps2PortCount > 0)
        Ps2Controller::Keyboard()->Init(false);
    if (ps2PortCount > 1)
        Ps2Controller::Mouse()->Init(true);
}

void InitDriverManagerAndPciTask()
{
    Kernel::Drivers::DriverManager::Global()->Init();
    Kernel::Devices::PciBridge::Global()->Init();
}

extern "C" void QueueInitTasks()
{
    using namespace Kernel::Scheduling;

    Scheduler::Global()->CreateThread((size_t)InitPs2Task, ThreadFlags::KernelMode)->Start(nullptr);
    Scheduler::Global()->CreateThread((size_t)InitDriverManagerAndPciTask, ThreadFlags::KernelMode)->Start(nullptr);
}
