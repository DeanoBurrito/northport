#include <hardware/x86_64/Private.hpp>
#include <hardware/x86_64/Cpuid.hpp>
#include <hardware/x86_64/Msr.hpp>
#include <hardware/x86_64/LocalApic.hpp>
#include <hardware/x86_64/PvClock.hpp>
#include <hardware/x86_64/Tsc.hpp>
#include <Core.hpp>
#include <EntryPrivate.hpp>
#include <Memory.hpp>
#include <Maths.hpp>

extern "C" char SysCallEntry[];
extern "C" char SysEnterEntry[];
extern "C" char BadSysCallEntry[];
extern "C" char InterruptStubsBegin[];
extern "C" char DebugEventEntry[];

namespace Npk
{
    static const uint64_t gdt[] =
    {
        0,                     //0x00 - null entry, required by spec
        0x00AF'9B00'0000'FFFF, //0x08 - kernel code
        0x00AF'9300'0000'FFFF, //0x10 - kernel data
        0,                     //0x18 - user code 32 (unused)
        0x00AF'FB00'0000'FFFF, //0x20 - user data (for sysret)
        0x00AF'F300'0000'FFFF, //0x28 - user code 64
        0x00AF'FB00'0000'FFFF, //0x30 - user data (for sysexit)
        0, 0                   //0x40-0x50 - TSS
    };
    
    static const struct SL_PACKED(
    {
        uint16_t limit = sizeof(gdt) - 1;
        uint64_t base = reinterpret_cast<uint64_t>(&gdt[0]);
    }) gdtr;

    constexpr size_t IdtSize = 0x100;

    struct IdtEntry
    {
        uint64_t data[2];
    } idt[IdtSize];

    static const struct SL_PACKED(
    {
        uint16_t limit = sizeof(idt) - 1;
        uint64_t base = reinterpret_cast<uint64_t>(&idt[0]);
    }) idtr;

    SL_NAKED_FUNC
    static void LoadGdt()
    {
        asm(R"(
            lgdt %0

            mov $0x10, %%ax
            mov %%ax, %%ds
            mov %%ax, %%ss
            mov %%ax, %%es
            mov %%ax, %%fs

            pop %%rdi
            push $0x08
            push %%rdi
            lretq
            )" :: "m"(gdtr) : "rax", "rdi");
    }

    void CommonCpuSetup()
    {
        LoadGdt();
        //TODO: load TSS

        for (size_t i = 0; i < IdtSize; i++)
        {

            uint64_t addr = (uintptr_t)InterruptStubsBegin + i * 16;
            if (i == DebugEventVector)
                addr = (uintptr_t)DebugEventEntry;
            idt[i].data[0] = (addr & 0xFFFF) | ((addr & 0xFFFF'0000) << 32);
            idt[i].data[0] |= 0x08 << 16; //kernel code selector
            idt[i].data[0] |= ((0b1110ull << 8) | (1ull << 15)) << 32; //present | type
            idt[i].data[1] = addr >> 32;
        }
        asm("lidt %0" :: "m"(idtr));

        uint64_t cr0 = READ_CR(0);
        cr0 &= ~(1 << 30); //clear CD
        cr0 &= ~(1 << 20); //clear NW
        cr0 |= 1 << 16; //set WP
        cr0 |= 1 << 5; //set NE
        cr0 |= 1 << 3; //set TS (tbh it doesnt matter at this stage)
        cr0 &= ~(1 << 2); //clear EM
        cr0 |= 1 << 1; //set MP
        WRITE_CR(0, cr0);

        uint64_t cr4 = READ_CR(4);
        cr4 &= ~(1 << 2); //clear TSD
        if (CpuHasFeature(CpuFeature::DebugExtensions))
            cr4 |= 1 << 3; //set DE
        cr4 &= ~(1 << 6); //clear MCE
        if (CpuHasFeature(CpuFeature::GlobalPages))
            cr4 |= 1 << 7;
        if (CpuHasFeature(CpuFeature::SSE))
        {
            cr4 |= 1 << 9; //set OXFXSR
            cr4 |= 1 << 10; //set OSXMMEXCPT
        }
        if (CpuHasFeature(CpuFeature::Umip))
            cr4 |= 1 << 11;
        if (CpuHasFeature(CpuFeature::WriteFsGsBase))
            cr4 |= 1 << 16; //enable fsgsbase
        if (CpuHasFeature(CpuFeature::XSave))
            cr4 |= 1 << 18; //enable xsave
        if (CpuHasFeature(CpuFeature::Smep))
            cr4 |= 1 << 20; //enable smep
        if (CpuHasFeature(CpuFeature::Smap))
            cr4 |= 1 << 21;
        WRITE_CR(4, cr4);

        uint64_t efer = ReadMsr(Msr::Efer);
        if (CpuHasFeature(CpuFeature::SysCall))
            efer |= 1 << 0; //enable syscall/sysret
        if (CpuHasFeature(CpuFeature::NoExecute))
            efer |= 1 << 11;
        WriteMsr(Msr::Efer, efer);

        Log("Control registers set: cr0=0x%tx, cr4=0x%tx, efer=0x%tx", 
                LogLevel::Verbose, cr0, cr4, efer);

        if (CpuHasFeature(CpuFeature::Smap))
            asm("clac" ::: "memory");

        if (CpuHasFeature(CpuFeature::SysCall))
        {
            WriteMsr(Msr::Star, ((0x18ull | 3) << 48) | (0x8ull << 32));
            WriteMsr(Msr::LStar, reinterpret_cast<uint64_t>(SysCallEntry));
            WriteMsr(Msr::CStar, reinterpret_cast<uint64_t>(BadSysCallEntry));
            WriteMsr(Msr::SFMask, 1 << 9); //will disable intrs on syscall entry

            Log("Setup for syscall/sysret: star=0x%tx, lstar=0x%tx, cstar=0x%tx, sfmask=0x%tx",
                LogLevel::Verbose, ReadMsr(Msr::Star), ReadMsr(Msr::LStar), 
                ReadMsr(Msr::CStar), ReadMsr(Msr::SFMask));
        }

        if (CpuHasFeature(CpuFeature::SysEnter))
        {
            WriteMsr(Msr::SysenterCs, 0x8);
            WriteMsr(Msr::SysenterRip, reinterpret_cast<uint64_t>(SysEnterEntry));
            WriteMsr(Msr::SysenterRsp, 0);

            Log("Setup for sysenter/sysexit: cs=0x%tx, rip=0x%tx, rsp=0x%tx",
                LogLevel::Verbose, ReadMsr(Msr::SysenterCs), ReadMsr(Msr::SysenterRip),
                ReadMsr(Msr::SysenterRsp));
        }

        //TODO: fixups: sse, xsave
        
        InitMachineChecking();
    }

    void HwSetMyLocals(uintptr_t where, CpuId softwareId)
    {
        auto tls = reinterpret_cast<CoreLocalHeader*>(where);
        tls->swId = softwareId;
        tls->selfAddr = where;
        tls->currThread = nullptr;
        tls->UnsafeFailurePath = nullptr;

        WriteMsr(Msr::GsBase, where);
        Log("Cpu %zu locals at %p", LogLevel::Info, softwareId, tls);
    }

    void HwPrimeThread(HwThreadContext** store, uintptr_t stub, uintptr_t entry, uintptr_t arg, uintptr_t stack)
    {
        SwitchFrame frame {};
        sl::MemSet(&frame, 0, sizeof(frame));
        frame.flags = 0x202;
        frame.rdi = reinterpret_cast<uint64_t>(arg);
        frame.rsi = reinterpret_cast<uint64_t>(entry);

        constexpr size_t NullLength = 16;

        sl::MemSet(reinterpret_cast<void*>(stack - NullLength), 0, NullLength);
        stack -= NullLength;
        sl::MemCopy(reinterpret_cast<void*>(stack - sizeof(stub)), &stub, sizeof(stub));
        stack -= sizeof(stub);

        auto* dest = reinterpret_cast<SwitchFrame*>(sl::AlignDown(stack, alignof(SwitchFrame)));
        dest--;
        sl::MemCopy(dest, &frame, sizeof(frame));

        *store = reinterpret_cast<HwThreadContext*>(dest);
    }

    void HwInitEarly()
    {
        const uint64_t dummyLocals = reinterpret_cast<uint64_t>(KERNEL_CPULOCALS_BEGIN);
        HwSetMyLocals(dummyLocals, 0);

        InitUarts();
        CommonCpuSetup();
    }

    static bool hasPvClocks;

    void ArchInitFull(uintptr_t& virtBase)
    {
        NPK_ASSERT(InitBspLapic(virtBase));
        InitReferenceTimers(virtBase);
        NPK_ASSERT(CalibrateTsc());

        if (CpuHasFeature(CpuFeature::VGuest))
            hasPvClocks = TryInitPvClocks(virtBase);
    }

    //NOTE: this function relies on rbp being used for the frame base
    //pointer, i.e. being compiled with `-fno-omit-frame-pointer`.
    size_t GetCallstack(sl::Span<uintptr_t> store, uintptr_t start, size_t offset)
    {
        struct Frame
        {
            Frame* next;
            uintptr_t returnAddress;
        };

        Frame* current = reinterpret_cast<Frame*>(start);
        if (start == 0)
            current = static_cast<Frame*>(__builtin_frame_address(0));

        for (size_t i = 0; i < offset; i++)
        {
            if (current == nullptr)
                return 0;
            current = current->next;
        }

        for (size_t i = 0; store.Size(); i++)
        {
            if (current == nullptr)
                return i;

            store[i] = current->returnAddress;
            current = current->next;
        }

        return store.Size();
    }

    void HwDumpPanicInfo(size_t maxWidth, size_t (*Print)(const char* format, ...))
    {
        (void)maxWidth;

        char brandBuffer[48]; //4 bytes per reg, 4 regs per bank, 3 banks
        size_t brandLen = GetBrandString(brandBuffer);

        if (brandLen != 0)
            Print("Brand: %.*s\n", (int)brandLen, brandBuffer);
    }


    void HwSetAlarm(sl::TimePoint expiry)
    {
        auto ticks = sl::TimeCount(expiry.Frequency, expiry.epoch).Rebase(
            MyTscFrequency()).ticks;
        ArmTscInterrupt(ticks);
    }

    sl::TimePoint HwReadTimestamp()
    {
        if (hasPvClocks)
            return { ReadPvSystemTime() };

        const auto timestamp = sl::TimeCount(MyTscFrequency(), ReadTsc());
        const auto ticks = timestamp.Rebase(sl::TimePoint::Frequency).ticks;
        return { ticks };
    }
    //ReadPvSystemTime() returns nanoseconds
    static_assert(sl::TimePoint::Frequency == sl::TimeScale::Nanos);

    void HwSendIpi(void* id)
    {
        const uint32_t apicId = reinterpret_cast<uintptr_t>(id);
        SendIpi(apicId, IpiType::Fixed, LapicIpiVector);
    }
}
