#pragma once

#include <stddef.h>
#include <Optional.h>
#include <Bitmap.h>
#include <containers/Vector.h>
#include <Platform.h>

namespace Kernel
{
    using InterruptCallback = void (*)(size_t vectorNumber, void* arg);

    struct InterruptAttachment
    {
        InterruptCallback callback = nullptr;
        void* arg = nullptr;
    };
    
    class InterruptManager
    {
    private:
        char lock;
        size_t allocOffset;
        sl::Bitmap allocBitmap;
        sl::Vector<InterruptAttachment> callbacks;

    public:
        static InterruptManager* Global();
        void Init();
        void Dispatch(size_t vector);

        [[nodiscard]]
        sl::Opt<size_t> AllocVectors(size_t count);
        void FreeVectors(size_t base, size_t count = 1);

        void AttachCallback(size_t vectorNumber, InterruptCallback func, void* argument);
        void DetachCallback(size_t vectorNumber);

        /*
            A note about MSIs: we return a full-sized address and data for future compatability,
            but PCI may not use all of this. At the time of writing, MSI(-X) only allows for 16-bits of
            data to be written. 
        */
        static sl::NativePtr GetMsiAddr(size_t processor);
        static NativeUInt GetMsiData(size_t vector);
    };
}
