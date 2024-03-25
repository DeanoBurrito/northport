#pragma once

#include <stddef.h>
#include <Optional.h>

namespace Npk::Interrupts
{
    enum class VectorPriority : size_t
    {
        Highest = 0,
        High = 1,
        Moderate = 2,
        Low = 3,
        Lowest = 4,
    };

    using InterruptVector = size_t;
    using VectorHandler = void (*)(InterruptVector vector, void* arg);
    
    constexpr size_t IvmZoneAll = -1ull;
    constexpr size_t IvmZoneLowPrior = -2ull;

    class InterruptVectorManager
    {
    private:
    public:
        InterruptVectorManager& Global();
        void Init();
        size_t WindowSize() const;

        bool Claim(InterruptVector vector);
        sl::Opt<InterruptVector> Alloc(size_t zone, sl::Opt<VectorPriority> priority);
        void Free(InterruptVector vector);

        bool Install(InterruptVector vector, VectorHandler handler);
        bool Uninstall(InterruptVector vector);
    };
}

using IVM = Npk::Interrupts::InterruptVectorManager;
