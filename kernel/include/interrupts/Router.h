#pragma once

#include <stddef.h>
#include <arch/Platform.h>
#include <tasking/RunLevels.h>
#include <containers/RBTree.h>
#include <containers/List.h>
#include <Locks.h>

namespace Npk::Interrupts
{
    struct CoreIntrRouting;

    struct InterruptRoute
    {
        sl::RBTreeHook hook;
        size_t core;
        size_t vector;

        void* callbackArg;
        bool (*Callback)(void* arg);
        Tasking::DpcStore* dpc;
    };

    struct IntrTreeLess
    {
        bool operator()(const InterruptRoute& a, const InterruptRoute& b)
        { return a.vector < b.vector; }
    };

    using IntrTree = sl::RBTree<InterruptRoute, &InterruptRoute::hook, IntrTreeLess>;

    class InterruptRouter
    {
    private:
        sl::RwLock routingsLock;
        sl::IntrFwdList<CoreIntrRouting> routings;

        CoreIntrRouting* GetRouting(size_t core);
        InterruptRoute* FindRoute(size_t core, size_t vector);

    public:
        static InterruptRouter& Global();

        void InitCore();
        void Dispatch(size_t vector);

        bool AddRoute(InterruptRoute* route, size_t core = NoCoreAffinity);
        bool ClaimRoute(InterruptRoute* route, size_t core, size_t vector);
        bool RemoveRoute(InterruptRoute* route);
    };
}

namespace Npk
{
    using Interrupts::InterruptRoute;

    static inline bool AddInterruptRoute(InterruptRoute* route, size_t core = NoCoreAffinity)
    { return Interrupts::InterruptRouter::Global().AddRoute(route, core); }

    static inline bool ClaimInterruptRoute(InterruptRoute* route, size_t core, size_t vector)
    { return Interrupts::InterruptRouter::Global().ClaimRoute(route, core, vector); }

    static inline bool RemoveInterruptRoute(InterruptRoute* route)
    { return Interrupts::InterruptRouter::Global().RemoveRoute(route); }
}
