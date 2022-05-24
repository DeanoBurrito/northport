#pragma once

#include <stddef.h>
#include <Optional.h>
#include <Bitmap.h>
#include <containers/Vector.h>
#include <Platform.h>

namespace Kernel
{
    using InterruptCallback = void (*)(size_t vectorNumber);
    
    class InterruptManager
    {
    private:
        char lock;
        size_t allocOffset;
        sl::Bitmap allocBitmap;
        sl::Vector<InterruptCallback> callbacks;

    public:
        static InterruptManager* Global();
        void Init();

        [[nodiscard]]
        sl::Opt<size_t> AllocVectors(size_t count);
        void FreeVectors(size_t base, size_t count = 1);

        void AttachCallback(size_t vectorNumber, InterruptCallback func);
        void DetachCallback(size_t vectorNumber);
    };
}
