#pragma once

#include <arch/Platform.h>
#include <Optional.h>
#include <Locks.h>

namespace Npk::Interrupts
{
    using IntVectorCallback = void (*)(size_t vector, void* arg);

    struct InterruptCallback
    {
        IntVectorCallback handler;
        void* arg;
    };

    class InterruptManager
    {
    private:
        uint8_t* allocBitmap;
        InterruptCallback* callbacks;
        size_t bitmapSize;
        size_t bitmapHint;
        sl::TicketLock lock;

    public:
        static InterruptManager& Global();
        void Init();
        void Dispatch(size_t vector);

        void Claim(size_t vector);
        sl::Opt<size_t> Alloc();
        void Free(size_t vector);
        void Attach(size_t vector, IntVectorCallback callback, void* arg);
        void Detach(size_t vector);
    };
}
