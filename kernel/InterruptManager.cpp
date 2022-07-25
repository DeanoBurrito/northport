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
        allocBitmap.Reset();
        
        while (callbacks.Size() < ALLOC_INT_VECTOR_COUNT)
            callbacks.EmplaceBack();
        
        sl::SpinlockRelease(&lock);
    }

    void InterruptManager::Dispatch(size_t vector)
    {
        Logf("Got int dispatch %u", LogSeverity::Debug, vector);
        if (vector < ALLOC_INT_VECTOR_BASE || vector - ALLOC_INT_VECTOR_BASE >= ALLOC_INT_VECTOR_COUNT)
            return;
        
        const size_t offset = vector - ALLOC_INT_VECTOR_BASE;
        if (callbacks.Size() < offset)
            return;

        if (callbacks[offset].callback != nullptr)
            callbacks[offset].callback(vector, callbacks[offset].arg);
        else
            Logf("No callback for allocated interrupt: vector=0x%x", LogSeverity::Error, vector);
    }

    sl::Opt<size_t> InterruptManager::AllocVectors(size_t count)
    {
        sl::ScopedSpinlock scopeLock(&lock);

        for (size_t i = 0; i < allocBitmap.Size(); i++)
        {
            if (allocBitmap.Get(i))
                continue;
            
            bool success = true;
            for (size_t j = 0; j < count; j++)
            {
                if (allocBitmap.Get(i + j))
                {
                    success = false;
                    i += j;
                    break;
                }
            }

            if (success)
            {
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

    void InterruptManager::AttachCallback(size_t vectorNumber, InterruptCallback func, void* arg)
    {
        sl::ScopedSpinlock scopeLock(&lock);
        callbacks[vectorNumber - allocOffset].callback = func;
        callbacks[vectorNumber - allocOffset].arg = arg;
    }

    void InterruptManager::DetachCallback(size_t vectorNumber)
    {
        sl::ScopedSpinlock scopeLock(&lock);
        callbacks[vectorNumber - allocOffset].callback = nullptr;
        callbacks[vectorNumber - allocOffset].arg = nullptr;
    }

    sl::NativePtr InterruptManager::GetMsiAddr(size_t processor)
    {
#ifdef __x86_64__
        //All other fields are fine as the default.
        //TODO: would be nice to use the redirection hint.
        return 0x0FEE'0000 | ((uint8_t)processor << 12);
#else
    #error "Unknown cpu architecture, cannot compile kernel/InterruptManager.cpp"
#endif
    }

    NativeUInt InterruptManager::GetMsiData(size_t vector)
    {
#ifdef __x86_64__
        //PCI defines MSIs are acting like they're edge-triggered.
        //All the other fields are fine as the default.
        return (uint16_t)vector;
#else
    #error "Unknown cpu architecture, cannot compile kernel/InterruptManager.cpp"
#endif
    }
}
