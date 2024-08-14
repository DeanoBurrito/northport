#pragma once

#include <NativePtr.h>
#include <Flags.h>
#include <Optional.h>
#include <Span.h>
#include <containers/Vector.h>

#ifdef NP_KERNEL
#include <filesystem/Filesystem.h>

/* This interface is provided by np-driverlib but it is also used within the kernel, 
 * albiet with a different implementation under the hood. This just makes it easier
 * to reason about driver code since VmObjects share the same properties.
 */
namespace Npk::Memory 
{
class VirtualMemoryManager;

using VMM = Npk::Memory::VirtualMemoryManager;
#else
#include <interfaces/driver/Filesystem.h>

namespace dl
{
#endif

    enum class VmFlag : size_t
    {
        Write = 0,
        Execute = 1,
        User = 2,
        Guarded = 3, 

        Anon = 24,
        Mmio = 25,
        File = 26,
    };

    constexpr size_t VmFlagTypeMask = 0xFF << 24;
    using VmFlags = sl::Flags<VmFlag>;

    struct VmFileArg
    {
#ifdef NP_KERNEL
        Npk::Filesystem::VfsId id;
#else
        npk_fs_id id;
#endif
        sl::StringSpan filepath;
        size_t offset;
        bool noDeferBacking;

        constexpr VmFileArg() : id(), offset(0), noDeferBacking(false)
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

    struct MdlPtr
    {
        size_t length;
        uintptr_t physAddr;
    };

    struct Mdl
    {
#ifdef NP_KERNEL
        VMM* vmm;
#endif
        sl::NativePtr base;
        size_t length;
        sl::Vector<MdlPtr> ptrs; //TODO: store inline with flexible array member?

#ifndef NP_KERNEL
        ~Mdl();
#endif
    };

    class VmObject
    {
    private:
#ifdef NP_KERNEL
        VMM* vmm = nullptr;
#endif
        sl::NativePtr base = nullptr;
        size_t size = 0;

    public:
        VmObject() = default;

        VmObject(size_t length, VmFlags flags)
            : VmObject(length, 0, flags, VmAllocLimits{})
        {}

        VmObject(size_t length, uintptr_t initArg, VmFlags flags)
            : VmObject(length, initArg, flags, VmAllocLimits{})
        {}

        VmObject(size_t length, VmFileArg& initArg, VmFlags flags)
            : VmObject(length, reinterpret_cast<uintptr_t>(&initArg), flags, VmAllocLimits{})
        {}

#ifdef NP_KERNEL
        VmObject(size_t length, uintptr_t initArg, VmFlags flags, VmAllocLimits limits)
            : VmObject(nullptr, length, initArg, flags, limits)
        {}

        VmObject(VMM* vmm, size_t length, VmFlags flags)
            : VmObject(vmm, length, 0, flags, VmAllocLimits{})
        {}

        VmObject(VMM* vmm, size_t length, uintptr_t initArg, VmFlags flags)
            : VmObject(vmm, length, initArg, flags, VmAllocLimits{})
        {}

        VmObject(VMM* vmm, size_t length, VmFileArg& initArg, VmFlags flags)
            : VmObject(vmm, length, reinterpret_cast<uintptr_t>(&initArg), flags, VmAllocLimits{})
        {}

        VmObject(VMM* vmm, size_t length, uintptr_t initArg, VmFlags flags, VmAllocLimits limits);
#else
        VmObject(size_t length, uintptr_t initArg, VmFlags flags, VmAllocLimits limits);
#endif

        ~VmObject();
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
        //instance itself. Can be used as a manual destructor.
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

#ifdef NP_KERNEL
using Npk::Memory::VMM;
using Npk::Memory::VmObject;
using Npk::Memory::VmFlag;
using Npk::Memory::VmFlagTypeMask;
using Npk::Memory::VmFlags;
using Npk::Memory::VmFileArg;
using Npk::Memory::VmAllocLimits;
using Npk::Memory::MdlPtr;
using Npk::Memory::Mdl;
#endif

