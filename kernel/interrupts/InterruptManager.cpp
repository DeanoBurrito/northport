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
        callbacks = new InterruptCallback[bitmapSize];

        for (size_t i = 0; i < bitmapSize; i++)
        {
            sl::BitmapClear(allocBitmap, i);
            callbacks[i].handler = nullptr;
            callbacks[i].arg = nullptr;
        }

        Log("Interrupt manager has %lu allocatable vectors.", LogLevel::Info, bitmapSize);
    }

    void InterruptManager::Dispatch(size_t vector)
    {
        ASSERT(vector >= IntVectorAllocBase && vector < IntVectorAllocLimit, "bad vector");

        vector -= IntVectorAllocBase;
        if (!sl::BitmapGet(allocBitmap, vector))
            return;
        
        if (callbacks[vector].handler == nullptr)
            Log("Vector %lu allocated and served without handler installed.", LogLevel::Error, vector + IntVectorAllocBase);
        else
            callbacks[vector].handler(vector, callbacks[vector].arg);
    }

    void InterruptManager::Claim(size_t vector)
    {
        ASSERT(vector >= IntVectorAllocBase && vector < IntVectorAllocLimit, "bad vector");

        sl::ScopedLock scopeLock(lock);
        if (sl::BitmapSet(allocBitmap, vector - IntVectorAllocBase))
            Log("Tried to claim interrupt vector %lu, but it's already claimed.", LogLevel::Error, vector);
        else
            callbacks[vector].handler = nullptr;
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
        ASSERT(vector >= IntVectorAllocBase && vector < IntVectorAllocLimit, "bad vector");
        vector -= IntVectorAllocBase;

        sl::ScopedLock scopeLock(lock);

        if (!sl::BitmapClear(allocBitmap, vector))
            Log("Interrupt vector %lu was double freed.", LogLevel::Error, vector + IntVectorAllocBase);
        callbacks[vector].handler = nullptr;
        callbacks[vector].arg = nullptr;
    }

    void InterruptManager::Attach(size_t vector, IntVectorCallback callback, void* arg)
    {
        ASSERT(vector >= IntVectorAllocBase && vector < IntVectorAllocLimit, "bad vector");
        vector -= IntVectorAllocBase;

        //managarm-inspired design :^)
        ASSERT(callback != nullptr, "Cannot attach nullptr as callback.");
        ASSERT(sl::BitmapGet(allocBitmap, vector), "Cannot attach callback to unallocated vector.");
        ASSERT(callbacks[vector].handler == nullptr, "Cannot overwrite existing interrupt vector callback.");

        callbacks[vector].handler = callback;
        callbacks[vector].arg = arg;
    }

    void InterruptManager::Detach(size_t vector)
    {
        ASSERT(vector >= IntVectorAllocBase && vector < IntVectorAllocLimit, "bad vector");
        vector -= IntVectorAllocBase;
        ASSERT(sl::BitmapGet(allocBitmap, vector), "Cannot detach callback from unallocated vector.");

        callbacks[vector].handler = nullptr;
        callbacks[vector].arg = nullptr;
    }
}
