#pragma once

#include <arch/Cpu.h>
#include <BufferView.h>

#ifdef __x86_64__
    #include <arch/x86_64/ArchPlatform.h>
#else
    #error "Unknown cpu architecture, cannot include platform specific defines. Refusing to compile."
#endif

#define MACRO_STR_INNER(x) #x
#define MACRO_STR(x) MACRO_STR_INNER(x)
#define FORCE_INLINE [[gnu::always_inline]] inline

namespace Kernel
{
    //the following definitions exist to enforce it's signature across architectures
    struct CoreLocalStorage;

    [[gnu::always_inline]] inline
    CoreLocalStorage* CoreLocal();

    struct StoredRegisters;

    extern NativeUInt hhdmBase;
    extern NativeUInt hhdmLength;
    sl::BufferView GetHhdm();

    /*
        The following are utility functions for converting between lower/higher halves
        of memory space.
        TODO: these are a pretty ugly solution. Would be nicer to get just get it right at the source
        of the pointer, instead of constantly casting them.
    */
    template<typename T>
    FORCE_INLINE T* EnsureHigherHalfAddr(T* existing)
    {
        if ((NativeUInt)existing < hhdmBase)
            return reinterpret_cast<T*>((NativeUInt)existing + hhdmBase);
        return existing;
    }

    FORCE_INLINE NativeUInt EnsureHigherHalfAddr(NativeUInt addr)
    {
        if (addr < hhdmBase)
            return addr + hhdmBase;
        return addr;
    }

    template<typename T>
    FORCE_INLINE T* EnsureLowerHalfAddr(T* existing)
    {
        if ((NativeUInt)existing >= hhdmBase)
            return reinterpret_cast<T*>((NativeUInt)existing - hhdmBase);
        return existing;
    }

    FORCE_INLINE NativeUInt EnsureLowerHalfAddr(NativeUInt addr)
    {
        if (addr >= hhdmBase)
            return addr - hhdmBase;
        return addr;
    }

    //Will protect a critical section that is sensitive to being interrupted.
    //Note that is does not stop things that ignore the interrupts flag, like NMIs or MCEs.
    class InterruptLock
    {
    private:
        bool previousState;
    public:
        InterruptLock()
        { 
            previousState = CPU::InterruptsEnabled(); 
            CPU::DisableInterrupts();
        }

        ~InterruptLock()
        {
            CPU::EnableInterrupts(previousState);
        }

        bool WillRestore()
        { return previousState; }

        void SetShouldRestore(bool newState)
        { previousState = newState; }
    };
}
