#pragma once

#include <NativePtr.h>

namespace Npk::Memory
{
    enum class VmFlags : size_t
    {
        None = 0,

        //access flags
        Write = 1 << 0,
        Execute = 1 << 1,
        User = 1 << 2,
        Global = 1 << 3,

        //bits 48-63 are the type of memory to be requested
        Anon = 1ul << 48,
        Mmio = 2ul << 48,
    };

    constexpr VmFlags operator|(const VmFlags& a, const VmFlags& b)
    { return (VmFlags)((uintptr_t)a | (uintptr_t)b); }

    constexpr VmFlags operator&(const VmFlags& a, const VmFlags& b)
    { return (VmFlags)((uintptr_t)a & (uintptr_t)b); }

    constexpr VmFlags operator|=(VmFlags& src, const VmFlags& other)
    { return src = (VmFlags)((uintptr_t)src | (uintptr_t)other); }

    constexpr VmFlags operator&=(VmFlags& src, const VmFlags& other)
    { return src = (VmFlags)((uintptr_t)src & (uintptr_t)other); }

    constexpr VmFlags operator~(const VmFlags& src)
    { return (VmFlags)(~(uintptr_t)src); }
    
    class VmObject
    {
    private:
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
    };
}

using Npk::Memory::VmObject;
using Npk::Memory::VmFlags;
