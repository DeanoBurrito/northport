#pragma once

#include <arch/Platform.h>
#include <Optional.h>
#include <Locks.h>

namespace Npk::Interrupts
{
    using IntVectorCallback = void (*)(size_t vector);

    class InterruptManager
    {
    private:
        uint8_t* allocBitmap;
        IntVectorCallback* callbacks;
        size_t bitmapSize;
        size_t bitmapHint;
        sl::TicketLock lock;

    public:
        static InterruptManager& Global();
        void Init();
        void Dispatch(size_t vector);

        sl::Opt<size_t> Alloc();
        void Free(size_t vector);
        void Attach(size_t vector, IntVectorCallback callback);
        void Detach(size_t vector);
    };
}
