#include <io/IntrRouter.h>
#include <debug/Log.h>

namespace Npk::Io
{
    struct CoreIntrRouting
    {
        CoreIntrRouting* next;

        size_t coreId;
        sl::InterruptLock lock;
        IntrTree tree;
    };
    
    CoreIntrRouting* InterruptRouter::GetRouting(size_t core)
    {
        if (core == CoreLocal().id)
            return static_cast<CoreIntrRouting*>(CoreLocal()[LocalPtr::IntrRouting]);

        routingsLock.ReaderLock();
        for (auto scan = routings.Begin(); scan != routings.End(); scan = scan->next)
        {
            if (scan->coreId != core)
                continue;

            routingsLock.ReaderUnlock();
            return scan;
        }

        routingsLock.ReaderUnlock();
        return nullptr;
    }

    InterruptRoute* InterruptRouter::FindRoute(size_t core, size_t vector)
    {
        CoreIntrRouting* routing = GetRouting(core);
        VALIDATE_(routing != nullptr, nullptr);

        InterruptRoute* scan = routing->tree.GetRoot();
        while (scan != nullptr)
        {
            if (scan->vector == vector)
                return scan;
            else if (vector < scan->vector)
                scan = IntrTree::GetLeft(scan);
            else
                scan = IntrTree::GetRight(scan);
        }

        return nullptr;
    }

    InterruptRouter globalInterruptRouter;
    InterruptRouter& InterruptRouter::Global()
    { return globalInterruptRouter; }

    void InterruptRouter::InitCore()
    {
        CoreIntrRouting* routing = new CoreIntrRouting();
        routing->coreId = CoreLocal().id;

        CoreLocal()[LocalPtr::IntrRouting] = routing;
        routingsLock.WriterLock();
        routings.PushBack(routing);
        routingsLock.WriterUnlock();
        Log("Interrupt routing initialized.", LogLevel::Info);
    }
    
    void InterruptRouter::Dispatch(size_t vector)
    {
        ASSERT_(CoreLocal().runLevel == RunLevel::Interrupt);

        CoreIntrRouting* routing = GetRouting(CoreLocal().id);
        routing->lock.Lock();
        InterruptRoute* route = FindRoute(CoreLocal().id, vector);
        routing->lock.Unlock();

        if (route == nullptr)
        {
            Log("Dropping interrupt, no route: %zu", LogLevel::Warning, vector);
            return;
        };

        bool queueDpc = true;
        if (route->Callback != nullptr)
            queueDpc = route->Callback(route->callbackArg);
        if (route->dpc != nullptr && queueDpc)
            Tasking::QueueDpc(route->dpc);
    }

    bool InterruptRouter::AddRoute(InterruptRoute* route, size_t core)
    {
        VALIDATE_(route != nullptr, false);

        if (core == NoCoreAffinity)
            core = CoreLocal().id; //TODO: balancing between cores (circular list?)
        route->vector = -1ul;
        route->core = core;

        CoreIntrRouting* routing = GetRouting(core);
        VALIDATE_(routing != nullptr, false);

        routing->lock.Lock();
        for (size_t i = IntVectorAllocBase; i < IntVectorAllocLimit; i++)
        {
            if (FindRoute(routing->coreId, i) != nullptr)
                continue; //TODO: more efficient searching

            route->vector = i;
            routing->tree.Insert(route);
            break;
        }
        routing->lock.Unlock();

        if (route->vector == -1ul)
            return false;

        Log("Interrupt route added: %zu:%zu, callback=%p, dpc=%p", LogLevel::Verbose,
            route->core, route->vector, route->Callback, route->dpc);
        return true;
    }

    bool InterruptRouter::ClaimRoute(InterruptRoute* route, size_t core, size_t gsi)
    {
        VALIDATE_(AddRoute(route, core), false);
        if (!RoutePinInterrupt(route->core, route->vector, gsi))
        {
            RemoveRoute(route); 
            return false;
        }

        Log("Pin-based interrupt claimed: gsi=%zu -> %zu:%zu", LogLevel::Verbose, gsi,
            route->core, route->vector);
        return true;
    }

    bool InterruptRouter::RemoveRoute(InterruptRoute* route)
    {
        VALIDATE_(route != nullptr, false);

        CoreIntrRouting* routing = GetRouting(route->core);
        VALIDATE_(routing != nullptr, false);

        routing->lock.Lock();
        routing->tree.Remove(route);
        routing->lock.Unlock();

        Log("Interrupt route removed: %zu:%zu, callback=%p, dpc=%p", LogLevel::Verbose,
            route->core, route->vector, route->Callback, route->dpc);

        route->core = NoCoreAffinity;
        route->vector = -1ul;
        return true;
    }

    sl::Opt<MsiConfig> InterruptRouter::GetMsi(InterruptRoute* route)
    {
        VALIDATE_(route != nullptr, {});

        //TODO: validate MSIs are supported
        //TODO: remove route for pin interrupt if present (need some flags for the route?)
        const MsiConfig config =
        {
            .address = MsiAddress(route->core, route->vector),
            .data = MsiData(route->core, route->vector),
        };

        return config;
    }
}
