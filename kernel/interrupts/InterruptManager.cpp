#include <interrupts/InterruptManager.h>
#include <debug/Log.h>
#include <Bitmap.h>
#include <Locks.h>

namespace Npk::Interrupts
{
    InterruptManager globalIntManager;
    InterruptManager& InterruptManager::Global()
    { return globalIntManager; } 

    void InterruptManager::Init()
    {
        bitmapSize = IntVectorAllocLimit - IntVectorAllocBase;
        bitmapHint = 0;
        allocBitmap = new uint8_t[sl::AlignUp(bitmapSize, 8) / 8];
        callbacks = new IntVectorCallback[bitmapSize];

        for (size_t i = 0; i < bitmapSize; i++)
        {
            sl::BitmapClear(allocBitmap, i);
            callbacks[i] = 0;
        }

        Log("Interrupt manager has %lu allocatable vectors.", LogLevel::Info, bitmapSize);
    }

    void InterruptManager::Dispatch(size_t vector)
    {
        ASSERT(vector < IntVectorAllocLimit, "Interrupt vector beyond allowed limit.");

        vector -= IntVectorAllocBase;
        if (!sl::BitmapGet(allocBitmap, vector) || callbacks[vector] == nullptr)
            return;
        
        callbacks[vector](vector);
    }
    
    sl::Opt<size_t> InterruptManager::Alloc()
    {
        sl::ScopedLock scopeLock(lock);
        for (size_t i = bitmapHint; i < bitmapSize; i++)
        {
            if (sl::BitmapGet(allocBitmap, i))
                continue;
            
            bitmapHint = i;
            sl::BitmapSet(allocBitmap, i);
            return i + IntVectorAllocBase;
        }

        if (bitmapHint == 0)
            return {};
        
        for (size_t i = 0; i < bitmapHint; i++)
        {
            if (sl::BitmapGet(allocBitmap, i))
                continue;
            
            bitmapHint = i;
            sl::BitmapSet(allocBitmap, i);
            return i + IntVectorAllocBase;
        }

        return {};
    }

    void InterruptManager::Free(size_t vector)
    {
        if (vector > IntVectorAllocLimit)
            return;
        vector -= IntVectorAllocBase;

        sl::ScopedLock scopeLock(lock);

        if (!sl::BitmapClear(allocBitmap, vector))
            Log("Interrupt vector %lu was double freed.", LogLevel::Error, vector + IntVectorAllocBase);
        callbacks[vector] = 0;
    }

    void InterruptManager::Attach(size_t vector, IntVectorCallback callback)
    {
        vector -= IntVectorAllocBase;
        ASSERT(callback != nullptr, "Cannot attach nullptr as callback.");
        ASSERT(sl::BitmapGet(allocBitmap, vector), "Cannot attach callback to unallocated vector.");
        ASSERT(callbacks[vector] == 0, "Cannot overwrite existing interrupt vector callback.");

        callbacks[vector] = callback;
    }

    void InterruptManager::Detach(size_t vector)
    {
        vector -= IntVectorAllocBase;
        ASSERT(sl::BitmapGet(allocBitmap, vector), "Cannot detach callback from unallocated vector.");

        callbacks[vector] = 0;
    }
}
