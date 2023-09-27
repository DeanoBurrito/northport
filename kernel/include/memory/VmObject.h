#pragma once

#include <NativePtr.h>
#include <Flags.h>
#include <Optional.h>
#include <Span.h>

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
    };

    class VmObject
    {
    private:
        VirtualMemoryManager* vmm = nullptr;
        sl::NativePtr base = nullptr;
        size_t size = 0;

    public:
        VmObject() = default;

        VmObject(size_t length, VmFlags flags) : VmObject(length, 0, flags)
        {};
        VmObject(size_t length, uintptr_t arg, VmFlags flags);

        ~VmObject();
        VmObject(const VmObject& other) = delete;
        VmObject& operator=(const VmObject& other) = delete;

        VmObject(VmObject&& from);
        VmObject& operator=(VmObject&& from);

        inline operator bool() const
        { return base.ptr != nullptr; }

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

        void Release();
        VmFlags Flags(sl::Opt<VmFlags> flags);
    };
}


using Npk::Memory::VmObject;
using Npk::Memory::VmFlag;
using Npk::Memory::VmFlags;
using VMM = Npk::Memory::VirtualMemoryManager;

