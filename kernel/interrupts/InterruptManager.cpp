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
            callbacks[i] = nullptr;
        }

        Log("Interrupt manager has %lu allocatable vectors.", LogLevel::Info, bitmapSize);
    }

    void InterruptManager::Dispatch(size_t vector)
    {
        ASSERT(vector >= IntVectorAllocBase && vector < IntVectorAllocLimit, "bad vector");

        vector -= IntVectorAllocBase;
        if (!sl::BitmapGet(allocBitmap, vector))
            return;
        
        VALIDATE(callbacks[vector] != nullptr,, "Allocated interrupt served, but no callback installed.")
        callbacks[vector](vector);
    }

    void InterruptManager::Claim(size_t vector)
    {
        ASSERT(vector >= IntVectorAllocBase && vector < IntVectorAllocLimit, "bad vector");

        sl::ScopedLock scopeLock(lock);
        if (sl::BitmapSet(allocBitmap, vector - IntVectorAllocBase))
            Log("Tried to claim interrupt vector %lu, but it's already claimed.", LogLevel::Error, vector);
        else
            callbacks[vector] = nullptr;
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
        callbacks[vector] = nullptr;
    }

    void InterruptManager::Attach(size_t vector, IntVectorCallback callback)
    {
        ASSERT(vector >= IntVectorAllocBase && vector < IntVectorAllocLimit, "bad vector");
        vector -= IntVectorAllocBase;

        //managarm-inspired design :^)
        ASSERT(callback != nullptr, "Cannot attach nullptr as callback.");
        ASSERT(sl::BitmapGet(allocBitmap, vector), "Cannot attach callback to unallocated vector.");
        ASSERT(callbacks[vector] == nullptr, "Cannot overwrite existing interrupt vector callback.");

        callbacks[vector] = callback;
    }

    void InterruptManager::Detach(size_t vector)
    {
        ASSERT(vector >= IntVectorAllocBase && vector < IntVectorAllocLimit, "bad vector");
        vector -= IntVectorAllocBase;
        ASSERT(sl::BitmapGet(allocBitmap, vector), "Cannot detach callback from unallocated vector.");

        callbacks[vector] = nullptr;
    }
}
