#pragma once

#include <NativePtr.h>
#include <Flags.h>
#include <Optional.h>
#include <Span.h>

namespace Npk::Memory { class VirtualMemoryManager; };
using VMM = Npk::Memory::VirtualMemoryManager;

namespace Npk::Memory
{
    enum class VmFlag : size_t
    {
        Write = 0,
        Execute = 1,
        User = 2,
        Guarded = 3, 

        Anon = 24, //bits 24-32 are reserved for the driver type
        Mmio = 25,
        File = 26,
    };

    using VmFlags = sl::Flags<VmFlag>;
    class VirtualMemoryManager;

    struct VmoFileInitArg
    {
        sl::StringSpan filepath;
        size_t offset;
        bool noDeferBacking;

        VmoFileInitArg() : filepath(), offset(0), noDeferBacking(false)
        {}
    };

    struct VmAllocLimits
    {
        uintptr_t lowerBound;
        uintptr_t upperBound;
        size_t alignment;

        constexpr VmAllocLimits() : lowerBound(0), upperBound(-1ul), alignment(1)
        {}
    };

    class VmObject
    {
    private:
        VMM* vmm = nullptr;
        sl::NativePtr base = nullptr;
        size_t size = 0;

    public:
        VmObject() = default;

        VmObject(size_t length, VmFlags flags)
            : VmObject(nullptr, length, 0, flags, VmAllocLimits{})
        {}

        VmObject(VMM* vmm, size_t length, VmFlags flags)
            : VmObject(vmm, length, 0, flags, VmAllocLimits{})
        {}

        VmObject(size_t length, uintptr_t initArg, VmFlags flags)
            : VmObject(nullptr, length, initArg, flags, VmAllocLimits{})
        {}

        VmObject(size_t length, VmoFileInitArg& initArg, VmFlags flags)
            : VmObject(nullptr, length, reinterpret_cast<uintptr_t>(&initArg), flags, VmAllocLimits{})
        {}

        VmObject(VMM* vmm, size_t length, uintptr_t initArg, VmFlags flags)
            : VmObject(vmm, length, initArg, flags, VmAllocLimits{})
        {}

        VmObject(VMM* vmm, size_t length, VmoFileInitArg& initArg, VmFlags flags)
            : VmObject(vmm, length, reinterpret_cast<uintptr_t>(&initArg), flags, VmAllocLimits{})
        {}

        VmObject(size_t length, uintptr_t initArg, VmFlags flags, VmAllocLimits limits)
            : VmObject(nullptr, length, initArg, flags, limits)
        {}

        VmObject(VMM* vmm, size_t length, uintptr_t initArg, VmFlags flags, VmAllocLimits limits);

        ~VmObject(); //TODO: investigate use of copy ctors?
        VmObject(const VmObject& other) = delete;
        VmObject& operator=(const VmObject& other) = delete;

        VmObject(VmObject&& from);
        VmObject& operator=(VmObject&& from);

        inline bool Valid() const
        { return base.ptr != nullptr; }

        inline sl::NativePtr& Ptr()
        { return base; }

        inline sl::NativePtr Ptr() const
        { return base; }

        inline sl::NativePtr& operator*()
        { return base; }

        inline sl::NativePtr operator*() const
        { return base; }

        inline sl::NativePtr* operator->()
        { return &base; }

        inline const sl::NativePtr* operator->() const
        { return &base; }

        inline size_t Size() const
        { return size; }

        inline sl::Span<uint8_t> Span()
        { return { base.As<uint8_t>(), size }; }

        inline sl::Span<const uint8_t> ConstSpan() const
        { return { base.As<const uint8_t>(), size}; }

        //releases the virtual memory managed by this VMO without the destroying the VMO
        //instance itself. Can be used as a manuall destructor.
        void Release();
        //used to set and retrieve the current flags of the attached virtual memory.
        VmFlags Flags(sl::Opt<VmFlags> flags);
        //portions this virtual memory into two separate blocks, either returning the portion before
        //or after the separation point. The remainder of the virtual memory is kept as part
        //of the current VMO, unless all memory is portioned off to the other - then this VMO
        //is released and effectively becomes uninitiallised (as it manages no memory).
        VmObject Subdivide(size_t length, bool fromStart);
    };
}


using Npk::Memory::VmObject;
using Npk::Memory::VmFlag;
using Npk::Memory::VmFlags;

