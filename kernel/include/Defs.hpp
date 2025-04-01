#pragma once

#include <Span.h>
#include <containers/List.h>
#include <Compiler.h>
#include <hardware/Arch.hpp>
#include <hardware/Plat.hpp>

extern "C" char KERNEL_CPULOCALS_BEGIN[];

namespace Npk
{
    template<typename T>
    class CpuLocal
    {
    private:
        alignas(T) char store[sizeof(T)];

    public:
        constexpr CpuLocal() = default;

        T& Get()
        {
            const uintptr_t base = ArchMyCpuLocals();
            const uintptr_t offset = reinterpret_cast<uintptr_t>(this) -
                reinterpret_cast<uintptr_t>(KERNEL_CPULOCALS_BEGIN);

            return *reinterpret_cast<T*>(base + offset);
        }

        T* operator&()
        {
             return &Get();
        }

        T& operator->()
        {
            return Get();
        }

        void operator=(const T& latest)
        {
            Get() = latest;
        }
    };

    class Event
    {
    private:
        size_t count;
        bool counting;

    public:
        void Reset(size_t count, size_t maxCount);
        void Signal(size_t count);
    };
}

#define CPU_LOCAL(T, id) SL_TAGGED(cpulocal, Npk::CpuLocal<T> id)
