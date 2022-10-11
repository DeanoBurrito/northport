#include <arch/riscv64/Sbi.h>

namespace Npk
{
    constexpr inline ulong EidBase = 0x10;
    constexpr inline ulong EidTime = 0x54494D45;
    constexpr inline ulong EidIpi = 0x735049;
    
    SbiRet SbiCall(SbiExt ext, ulong fid, ulong arg0, ulong arg1, ulong arg2)
    {
        //register keyword + asm labels causes these variables to be pinned to the specified registers. 
        register ulong a7 asm("a7") = (ulong)ext;
        register ulong a6 asm("a6") = fid;
        register ulong a0 asm("a0") = arg0;
        register ulong a1 asm("a1") = arg1;
        register ulong a2 asm("a2") = arg2;

        asm volatile("ecall" : "+r"(a0), "+r"(a1) : "r"(a7), "r"(a6), "r"(a2) : "memory");

        return { a0, a1 };
    }

    ulong GetSbiVersion()
    {
        return SbiCall(SbiExt::Base, 0, 0, 0, 0).value;
    }

    bool SbiExtensionAvail(SbiExt eid)
    {
        return SbiCall(SbiExt::Base, 3, (ulong)eid, 0, 0).value != 0;
    }

    void SbiSetTimer(ulong deadline)
    {
        SbiCall(SbiExt::Time, 0, deadline, 0, 0);
    }

    void SbiSendIpi(ulong mask, ulong maskBase)
    {
        SbiCall(SbiExt::SupervisorIpi, 0, mask, maskBase, 0);
    }

    bool SbiStartHart(ulong hartId, ulong startAddr, ulong data)
    {
        return SbiCall(SbiExt::Hsm, 0, hartId, startAddr, data).error == 0;
    }

    SbiResetError SbiResetSystem(SbiResetType resetType, SbiResetReason reason)
    {
        return (SbiResetError)SbiCall(SbiExt::Reset, 0, (ulong)resetType, (ulong)reason, 0).error;
    }
}
