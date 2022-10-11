#pragma once

namespace Npk
{
    using ulong = unsigned long; //tell me you like c# without telling me you like c#.

    struct SbiRet
    {
        ulong error;
        ulong value;
    };

    enum class SbiExt : ulong
    {
        Base = 0x10,
        Time = 0x54494D45,
        SupervisorIpi = 0x735049,
        Hsm = 0x48534D,
        Reset = 0x53525354,
    };

    enum class SbiResetType : ulong
    {
        Shutdown  = 0,
        ColdReboot = 1,
        WarmReboot = 2
    };

    enum class SbiResetReason : ulong
    {
        NoReason = 0,
        SystemFailure = 1,
    };

    enum class SbiResetError : ulong
    {
        InvalidParam = (ulong)-3,
        NotSupported = (ulong)-2,
        MiscFailure = (ulong)-1,
    };

    SbiRet SbiCall(SbiExt ext, ulong fid, ulong a, ulong b, ulong c);

    ulong GetSbiVersion();
    bool SbiExtensionAvail(SbiExt eid);
    void SbiSetTimer(ulong deadline);
    void SbiSendIpi(ulong mask, ulong maskBase);
    bool SbiStartHart(ulong hartId, ulong startAddr, ulong data);
    SbiResetError SbiResetSystem(SbiResetType resetType, SbiResetReason reason);

}
