#include <hardware/x86_64/Private.hpp>
#include <hardware/x86_64/Cpuid.hpp>
#include <hardware/x86_64/Msr.hpp>
#include <Core.hpp>

namespace Npk
{
    constexpr size_t BankMsrStride = 4;
    constexpr uint64_t MciStatusAddrv = 1ull << 58;
    constexpr uint64_t MciStatusMiscv = 1ull << 59;
    constexpr uint64_t MciStatusv = 1ull << 63;

    static size_t mcBanks;
    static bool mcCtrlSupport;

    static void LogAndClearBankStatus(size_t bank)
    {
        auto reg = (uint32_t)Msr::MciStatus + bank * BankMsrStride;
        auto status = ReadMsr((Msr)reg);
        WriteMsr((Msr)reg, 0);

        if ((status & MciStatusv) == 0)
            return;

        Log("Machine check error, bank %zu: 0x%" PRIx64, LogLevel::Error,
            bank, status);

        if (status & MciStatusAddrv)
        {
            auto addr = ReadMsr((Msr)(reg + 1));
            Log("Bank %zu addr value: 0x%" PRIx64, LogLevel::Error, bank, addr);
        }
        if (status & MciStatusMiscv)
        {
            auto addr = ReadMsr((Msr)(reg + 2));
            Log("Bank %zu misc value: 0x%" PRIx64, LogLevel::Error, bank, addr);
        }

    }

    void InitMachineChecking()
    {
        if (!CpuHasFeature(CpuFeature::Mca))
            return;
        if (!CpuHasFeature(CpuFeature::Mce))
            return;

        const uint64_t caps = ReadMsr(Msr::McgCap);
        mcBanks = caps & 0xFF;
        mcCtrlSupport = caps & 0x100;

        if (mcCtrlSupport)
            WriteMsr(Msr::McgCtl, -1);

        for (size_t i = 0; i < mcBanks; i++)
        {
            auto msrBase = (uint32_t)Msr::MciCtl + BankMsrStride * i;

            //TODO: intel states we should leave mc0ctl alone in p6 family processors, as its aliased to another msr

            //enable reporting for all errors, the cpu will drop writes
            //to unsupported bits.
            WriteMsr((Msr)msrBase, -1);

            LogAndClearBankStatus(i);
        }

        uint64_t cr4 = READ_CR(4);
        cr4 |= 1 << 6;
        WRITE_CR(4, cr4);
        
        Log("Machine check architecture enabled.", LogLevel::Info);
    }

    void HandleMachineCheckException(TrapFrame* frame)
    {
        const uint64_t status = ReadMsr(Msr::McgStatus);
        const bool errorIpValid = status & 0b10;
        const bool restartIpValid = status & 0b1;

        //make sure to clear bit 2 (MCIP), hardware will set this
        //when serving a MCE unless this bit is already set, in which
        //case it will shutdown the cpu.
        WriteMsr(Msr::McgStatus, status & ~0b100);

        if (errorIpValid)
        {
            Log("Machine-check relevant IP: 0x%tx", LogLevel::Error, 
                frame->iret.rip);
        }
        for (size_t i = 0; i < mcBanks; i++)
            LogAndClearBankStatus(i);

        if (!restartIpValid)
            Panic("Non-restartable machine-check exception", frame);
    }
}
