#include <InterruptManager.h>
#include <Maths.h>
#include <Locks.h>
#include <Log.h>

namespace Kernel
{
    InterruptManager* globalInterruptManager = nullptr;
    InterruptManager* InterruptManager::Global()
    {
        if (globalInterruptManager == nullptr)
            globalInterruptManager = new InterruptManager;
        return globalInterruptManager;
    }

    void InterruptManager::Init()
    {
        allocOffset = ALLOC_INT_VECTOR_BASE;
        allocBitmap.Resize(ALLOC_INT_VECTOR_COUNT);
        
        while (callbacks.Size() < ALLOC_INT_VECTOR_COUNT)
            callbacks.EmplaceBack(nullptr);
        
        sl::SpinlockRelease(&lock);
    }

    sl::Opt<size_t> InterruptManager::AllocVectors(size_t count)
    {
        sl::ScopedSpinlock scopeLock(&lock);

        for (size_t i = 0; i < allocBitmap.Size(); i++)
        {
            if (allocBitmap.Get(i))
                continue;
            
            for (size_t j = 0; j < count; j++)
            {
                if (allocBitmap.Get(i + j))
                    break;

                for (size_t k = 0; k < count; k++)
                    allocBitmap.Set(i + k);
                
                Logf("Allocating interrupt vector range: base=0x%x, count=0x%x", LogSeverity::Verbose, i + allocOffset, count);
                return i + allocOffset;
            }
        }

        return {};
    }

    void InterruptManager::FreeVectors(size_t base, size_t count)
    {
        base = sl::max(base, allocOffset);
        count = sl::min(base + count, allocOffset + allocBitmap.Size());

        sl::ScopedSpinlock scopeLock(&lock);
        for (size_t i = base; i < count; i++)
            allocBitmap.Clear(i);
    }

    void InterruptManager::AttachCallback(size_t vectorNumber, InterruptCallback func)
    {
        sl::ScopedSpinlock scopeLock(&lock);
        callbacks[vectorNumber - allocOffset] = func;
    }

    void InterruptManager::DetachCallback(size_t vectorNumber)
    {
        sl::ScopedSpinlock scopeLock(&lock);
        callbacks[vectorNumber - allocOffset] = nullptr;
    }
}
