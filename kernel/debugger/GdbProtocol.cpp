#include <private/Debugger.hpp>
#include <lib/Memory.hpp>
#include <lib/Maths.hpp>
#include <lib/Printf.hpp>

/* This file implements support for driving the kernel debugger via the GDB
 * remote protocol (sometimes called the GDB serial protocol).
 *
 * Resources used:
 * - sourceware.org/gdb/current/onlinedocs/gdb.html/Remote-Protocol.html
 * - https://github.com/bminor/binutils-gdb/tree/master/gdb/stubs
 * - https://github.com/OBOS-dev/obos/tree/master/src/oboskrnl/arch/x86_64/gdbstub
 * - GDB has an option (`debug remote`) that is very helpful for debugging this
 *   from the GDB-side of things.
 */
namespace Npk::Private
{
    constexpr const char* GdbRegTypeInt32 = "int32";
    constexpr const char* GdbRegTypeInt64 = "int64";
    constexpr const char* GdbRegTypeDataPtr = "data_ptr";
    constexpr const char* GdbRegTypeCodePtr = "code_ptr";

    struct GdbArchReg
    {
        bool valid; //whether register is valid on the current system.
        HwReg hwReg; //kernel specific data, see `Hardware.hpp` for details.
        const char* name; //friendly name
        const char* gdbType; //GDB XML type (how it should be displayed)
        uint8_t bits; //static width, used when `valid` is clear.
    };

    /* `GdbArchRegs` maps GDB register numbers to the northport equivalent,
     * plus storing some extra metadata about the register. Registers marked
     * invalid (!valid) are known to GDB and the architecture but dont exist
     * on the current system. For invalid registers the static register width
     * defined here is used for communicating with GDB, since 
     * `HwGetRegisterWidth()` will always return 0 for them.
     */

#ifdef __x86_64__
    constexpr const char* GdbRegTypeI386Flags = "x86_flags";
    constexpr const char* GdbRegTypeI387Ext = "i387_ext";

    constexpr GdbArchReg GdbArchRegs[] =
    {
        { true, HwReg_rax, "rax", GdbRegTypeInt64, 64 },
        { true, HwReg_rbx, "rbx", GdbRegTypeInt64, 64 },
        { true, HwReg_rcx, "rcx", GdbRegTypeInt64, 64 },
        { true, HwReg_rdx, "rdx", GdbRegTypeInt64, 64 },
        { true, HwReg_rsi, "rsi", GdbRegTypeInt64, 64 },
        { true, HwReg_rdi, "rdi", GdbRegTypeInt64, 64 },
        { true, HwReg_rbp, "rbp", GdbRegTypeDataPtr, 64 },
        { true, HwReg_rsp, "rsp", GdbRegTypeDataPtr, 64 },
        { true, HwReg_r8 , "r8", GdbRegTypeInt64, 64 },
        { true, HwReg_r9 , "r9", GdbRegTypeInt64, 64 },
        { true, HwReg_r10, "r10", GdbRegTypeInt64, 64 },
        { true, HwReg_r11, "r11", GdbRegTypeInt64, 64 },
        { true, HwReg_r12, "r12", GdbRegTypeInt64, 64 },
        { true, HwReg_r13, "r13", GdbRegTypeInt64, 64 },
        { true, HwReg_r14, "r14", GdbRegTypeInt64, 64 },
        { true, HwReg_r15, "r15", GdbRegTypeInt64, 64 },
        { true, HwReg::ProgramCounter, "rip", GdbRegTypeCodePtr, 64 },
        { true, HwReg::Flags, "eflags", GdbRegTypeI386Flags, 32 },
        { true, HwReg_cs, "cs", GdbRegTypeInt32, 32 },
        { true, HwReg_ss, "ss", GdbRegTypeInt32, 32 },
        { true, HwReg_ds, "ds", GdbRegTypeInt32, 32 },
        { true, HwReg_es, "es", GdbRegTypeInt32, 32 },
        { true, HwReg_fs, "fs", GdbRegTypeInt32, 32 },
        { true, HwReg_gs, "gs", GdbRegTypeInt32, 32 },
        { true, HwReg_st0, "st0", GdbRegTypeI387Ext, 80 },
        { true, HwReg_st1, "st1", GdbRegTypeI387Ext, 80 },
        { true, HwReg_st2, "st2", GdbRegTypeI387Ext, 80 },
        { true, HwReg_st3, "st3", GdbRegTypeI387Ext, 80 },
        { true, HwReg_st4, "st4", GdbRegTypeI387Ext, 80 },
        { true, HwReg_st5, "st5", GdbRegTypeI387Ext, 80 },
        { true, HwReg_st6, "st6", GdbRegTypeI387Ext, 80 },
        { true, HwReg_st7, "st7", GdbRegTypeI387Ext, 80 },
        { true, HwReg_fctrl, "fctrl", GdbRegTypeInt32, 32 },
        { true, HwReg_fstat, "fstat", GdbRegTypeInt32, 32 },
        { true, HwReg_ftag, "ftag", GdbRegTypeInt32, 32 },
        { false, HwReg_fiseg, "fiseg", GdbRegTypeInt32, 32 },
        { false, HwReg_fioff, "fioff", GdbRegTypeInt32, 32 },
        { false, HwReg_foseg, "foseg", GdbRegTypeInt32, 32 },
        { false, HwReg_fooff, "fooff", GdbRegTypeInt32, 32 },
        { false, HwReg_fop, "fop", GdbRegTypeInt32, 32 },
        { true, HwReg_xmm0, "xmm0", "vec128", 128 },
        { true, HwReg_xmm1, "xmm1", "vec128", 128 },
        { true, HwReg_xmm2, "xmm2", "vec128", 128 },
        { true, HwReg_xmm3, "xmm3", "vec128", 128 },
        { true, HwReg_xmm4, "xmm4", "vec128", 128 },
        { true, HwReg_xmm5, "xmm5", "vec128", 128 },
        { true, HwReg_xmm6, "xmm6", "vec128", 128 },
        { true, HwReg_xmm7, "xmm7", "vec128", 128 },
        { true, HwReg_xmm8, "xmm8", "vec128", 128 },
        { true, HwReg_xmm9, "xmm9", "vec128", 128 },
        { true, HwReg_xmm10, "xmm10", "vec128", 128 },
        { true, HwReg_xmm11, "xmm11", "vec128", 128 },
        { true, HwReg_xmm12, "xmm12", "vec128", 128 },
        { true, HwReg_xmm13, "xmm13", "vec128", 128 },
        { true, HwReg_xmm14, "xmm14", "vec128", 128 },
        { true, HwReg_xmm15, "xmm15", "vec128", 128 },
        { true, HwReg_mxcsr, "mxcsr", "x86_mxcsr", 32 },
    };

    constexpr const char* GdbArchName = "i386:x86-64";
    constexpr const char* GdbArchFeature = "org.gnu.gdb.i386.core";
    constexpr size_t GdbArchRegCount =
        sizeof(GdbArchRegs) / sizeof(GdbArchRegs[0]);

    constexpr const char* GdbArchHeader =
        "    <flags id=\"x86_flags\" size=\"4\">\n"
        "      <field name=\"CF\" start=\"0\" end=\"0\"/>\n"
        "      <field name=\"PF\" start=\"2\" end=\"2\"/>\n"
        "      <field name=\"AF\" start=\"4\" end=\"4\"/>\n"
        "      <field name=\"ZF\" start=\"6\" end=\"6\"/>\n"
        "      <field name=\"SF\" start=\"7\" end=\"7\"/>\n"
        "      <field name=\"TF\" start=\"8\" end=\"8\"/>\n"
        "      <field name=\"IF\" start=\"9\" end=\"9\"/>\n"
        "      <field name=\"DF\" start=\"10\" end=\"10\"/>\n"
        "      <field name=\"OF\" start=\"11\" end=\"11\"/>\n"
        "      <field name=\"IOPL\" start=\"12\" end=\"13\"/>\n"
        "      <field name=\"NT\" start=\"14\" end=\"14\"/>\n"
        "      <field name=\"RF\" start=\"16\" end=\"16\"/>\n"
        "      <field name=\"VM\" start=\"17\" end=\"17\"/>\n"
        "      <field name=\"AC\" start=\"18\" end=\"18\"/>\n"
        "      <field name=\"VIF\" start=\"19\" end=\"19\"/>\n"
        "      <field name=\"VIP\" start=\"20\" end=\"20\"/>\n"
        "      <field name=\"ID\" start=\"21\" end=\"21\"/>\n"
        "    </flags>\n"
        "    <vector id=\"v4f\" type=\"ieee_single\" count=\"4\"/>\n"
        "    <vector id=\"v2d\" type=\"ieee_double\" count=\"2\"/>\n"
        "    <vector id=\"v16i8\" type=\"int8\" count=\"16\"/>\n"
        "    <vector id=\"v8i16\" type=\"int16\" count=\"8\"/>\n"
        "    <vector id=\"v4i32\" type=\"int32\" count=\"4\"/>\n"
        "    <vector id=\"v2i64\" type=\"int64\" count=\"2\"/>\n"
        "    <union id=\"vec128\">\n"
        "      <field name=\"v4_float\" type=\"v4f\"/>\n"
        "      <field name=\"v2_double\" type=\"v2d\"/>\n"
        "      <field name=\"v16_int8\" type=\"v16i8\"/>\n"
        "      <field name=\"v8_int16\" type=\"v8i16\"/>\n"
        "      <field name=\"v4_int32\" type=\"v4i32\"/>\n"
        "      <field name=\"v2_int64\" type=\"v2i64\"/>\n"
        "    </union>\n"
        "    <flags id=\"x86_mxcsr\" size=\"4\">\n"
        "      <field name=\"IE\" start=\"0\" end=\"0\"/>\n"
        "      <field name=\"DE\" start=\"1\" end=\"1\"/>\n"
        "      <field name=\"ZE\" start=\"2\" end=\"2\"/>\n"
        "      <field name=\"OE\" start=\"3\" end=\"3\"/>\n"
        "      <field name=\"UE\" start=\"4\" end=\"4\"/>\n"
        "      <field name=\"PE\" start=\"5\" end=\"5\"/>\n"
        "      <field name=\"DAZ\" start=\"6\" end=\"6\"/>\n"
        "      <field name=\"IM\" start=\"7\" end=\"7\"/>\n"
        "      <field name=\"DM\" start=\"8\" end=\"8\"/>\n"
        "      <field name=\"ZM\" start=\"9\" end=\"9\"/>\n"
        "      <field name=\"OM\" start=\"10\" end=\"10\"/>\n"
        "      <field name=\"UM\" start=\"11\" end=\"11\"/>\n"
        "      <field name=\"PM\" start=\"12\" end=\"12\"/>\n"
        "      <field name=\"FZ\" start=\"15\" end=\"15\"/>\n"
        "    </flags>\n";
#else
#error "GdbRemote.cpp: Unknown architecture, lacking register table"
#endif
}

namespace Npk::Private
{
    using RwBuffer = sl::Span<uint8_t>;
    using RoBuffer = sl::Span<const uint8_t>;

    constexpr size_t RecvBufSize = 0x1000;
    constexpr size_t SendBufSize = 0x2000;
    constexpr size_t MaxRegBytes = 64;
    constexpr size_t SendTryCount = 16;

    constexpr uint8_t GdbRleBase = 28;
    constexpr uint8_t GdbRleEscape = '}';
    constexpr size_t GdbRleMinRun = 4;
    constexpr size_t GdbRleMaxRun = 98;

    constexpr uintptr_t GdbProtoAnyThread = 0;
    constexpr CpuId AllThreads = (CpuId)-1;
    constexpr CpuId AnyThread = (CpuId)-2;

    constexpr size_t CpuStoreFlags = 0;
    constexpr size_t CpuStoreRangeStart = 1;
    constexpr size_t CpuStoreRangeEnd = 2;

    enum class GdbError : uint8_t
    {
        MissingArg = 1,
        InvalidArg = 2,
        InternalError = 3,
        BadAddress = 4,
    };

    struct GdbData
    {
        DebugTransport* transport;
        uint8_t recvBuf[RecvBufSize];
        uint8_t sendBuf[SendBufSize];

        CpuId selectedThread; //for g/G/p/P packets
        CpuId continueThread; //for Hc/Hs packets
        CpuId threadListHead; //for qfThreadInfo/qsThreadInfo packets

        TrapFrame* stopFrame;
        uint8_t stopSignal;
        Breakpoint* stopBreakpoint;
        BreakpointType stopBreakpointType;

        bool noAck;
        bool breakCommandLoop;

        struct
        {
            bool swBreak;
            bool hwBreak;
            bool errorMsg;
            bool xmlFeatures;
        } peerFeatures;
    };

    struct GdbBreakpointArgs
    {
        uint8_t type;
        uintptr_t address;
        uintptr_t kind;
    };

    static GdbData gdbInst;

    constexpr static Breakpoint GdbBreakpointTemplates[] =
    {
        { .listHook = {}, .arch = {}, .addr = {}, .kind = {}, 
            .read = false, .write = false, .execute = true , .hardware = false},
        { .listHook = {}, .arch = {}, .addr = {}, .kind = {}, 
            .read = false, .write = false, .execute = true , .hardware = true},
        { .listHook = {}, .arch = {}, .addr = {}, .kind = {}, 
            .read = false, .write = true, .execute = false, .hardware = false },
        { .listHook = {}, .arch = {}, .addr = {}, .kind = {}, 
            .read = true, .write = false, .execute = false, .hardware = false },
        { .listHook = {}, .arch = {}, .addr = {}, .kind = {}, 
            .read = true, .write = true, .execute = false, .hardware = false },
    };

    constexpr size_t TemplateCount = sizeof(GdbBreakpointTemplates) 
        / sizeof(GdbBreakpointTemplates[0]);

    static bool IsHexDigit(uint8_t c)
    {
        return (c >= '0' && c <= '9')
            || (c >= 'a' && c <= 'f')
            || (c >= 'A' && c <= 'F');
    }

    static uint8_t FromHex(uint8_t c)
    {
        if (c >= '0' && c <= '9')
            return c - '0';

        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;

        return c - 'A' + 10;
    }

    static uint8_t ToHex(uint8_t n)
    {
        return "0123456789abcdef"[n & 0xF];
    }

    //returns number of characters read from buffer.
    static size_t ParseHexInt(uintptr_t& out, RoBuffer buffer)
    {
        out = 0;

        size_t i = 0;
        while (i < buffer.Size() && IsHexDigit(buffer[i]))
        {
            out = (out << 4) | FromHex(buffer[i]);
            i++;
        }

        return i;
    }

    //returns number of characters written to buffer
    static size_t PutHexInt(RwBuffer buffer, uintptr_t val, size_t minDigits)
    {
        char buff[18];
        const size_t len = sl::SnPrintf(buff, sizeof(buff), "%zx", (size_t)val);

        if (len < minDigits)
        {
            const size_t pad = minDigits - len;
            if (pad + len + 1 > buffer.Size())
                return 0;

            sl::MemSet(buffer.Begin(), '0', pad);
            sl::MemCopy(buffer.Begin() + pad, buff, len);

            return pad + len;
        }

        if (len + 1 > buffer.Size())
            return 0;

        sl::MemCopy(buffer.Begin(), buff, len);

        return len;
    }

    //returns number of characters read from buffer.
    static size_t ParseThreadId(CpuId& outId, RoBuffer buffer)
    {
        if (buffer.Size() >= 2 && buffer[0] == '-' && buffer[1] == '1')
        {
            outId = AllThreads;

            return 2;
        }

        uintptr_t value;
        const size_t lenRead = ParseHexInt(value, buffer);
        if (lenRead == 0)
        {
            outId = AnyThread;

            return 0;
        }

        outId = value - 1;
        if (value == GdbProtoAnyThread)
            outId = AnyThread;

        return lenRead;
    }

    //returns number of characters written to buffer
    static size_t PutThreadId(RwBuffer buffer, CpuId id)
    {
        if (id == AllThreads)
        {
            if (buffer.Size() < 2)
                return 0;

            buffer[0] = '-';
            buffer[1] = '1';

            return 2;
        }

        uintptr_t value = id + 1;
        if (id == AnyThread)
            value = GdbProtoAnyThread;

        return PutHexInt(buffer, value, 1);
    }

    static NpkStatus RawSend(GdbData& inst, RoBuffer data)
    {
        for (size_t i = 0; i < SendTryCount; i++)
        {
            if (!inst.transport->Transmit(inst.transport, data))
                return NpkStatus::InternalError;

            if (inst.noAck)
                return NpkStatus::Success;

            //wait for response.
            while (true)
            {
                uint8_t ack;
                size_t n = inst.transport->Receive(inst.transport, { &ack, 1 });

                if (n == 0)
                    continue;
                if (ack == '+')
                    return NpkStatus::Success;
                if (ack == '-')
                    break;
            }
        }

        return NpkStatus::NotAvailable;
    }

    //returns number of characters written to `*src`, this function is safe to
    //call with `dest` and `src` pointing to the same buffer. It applies the
    //RLE described in the gdb remote protocol spec, which includes a few 
    //special cases.
    static size_t RleEncode(uint8_t* dest, const uint8_t* src, size_t len)
    {
        size_t write = 0;
        size_t read = 0;

        while (read < len)
        {
            const uint8_t c = src[read];

            size_t runLength = 1;
            while (read + runLength < len && src[read + runLength] == c
                && runLength < GdbRleMaxRun)
                runLength++;

            size_t count = runLength;
            while (count >= GdbRleMinRun)
            {
                const uint8_t cc = (uint8_t)(count + GdbRleBase);

                //handle special cases, these characters have other meanings so
                //we shouldn't use them for RLE counts.
                if (cc != '#' && cc != '$' && cc != '*'
                    && cc != GdbRleEscape)
                    break;

                count--;
            }

            if (count >= GdbRleMinRun)
            {
                dest[write++] = c;
                dest[write++] = '*';
                dest[write++] = static_cast<uint8_t>(count + GdbRleBase);

                read += count;
            }
            else
                dest[write++] = src[read++];
        }

        return write;
    }

    //returns the expanded length (write head) of the encoded data, or 0 if
    //an error occured like `destLen` being too small.
    static size_t RleDecode(uint8_t* dest, size_t destLen, const uint8_t* src,
        size_t srcLen)
    {
        size_t write = 0;
        size_t read = 0;
        uint8_t prev = 0;

        while (read < srcLen)
        {
            if (src[read] == GdbRleEscape && read + 1 < srcLen)
            {
                if (write >= destLen)
                    return 0;

                prev = src[read + 1] ^ 0x20;
                dest[write++] = prev;

                read += 2;
            }
            else if (src[read] == '*' && read + 1 < srcLen)
            {
                const uint8_t countChar = src[read + 1];
                if (countChar < ' ')
                    return 0;

                const size_t extra = (countChar - GdbRleBase) - 1;
                if (write + extra > destLen)
                    return 0;

                for (size_t j = 0; j < extra; j++)
                    dest[write++] = prev;

                read += 2;
            }
            else
            {
                if (write >= destLen)
                    return 0;

                prev = src[read];
                dest[write++] = prev;

                read++;
            }
        }

        return write;
    }

    static NpkStatus FlushPacket(GdbData& inst, size_t head)
    {
        if (head + 2 > SendBufSize)
            return NpkStatus::InvalidArg;
        if (head < 2 || inst.sendBuf[0] != '$' || inst.sendBuf[head - 1] != '#')
            return NpkStatus::InvalidArg;

        auto* content = inst.sendBuf + 1;
        const size_t rawLen = head - 2;
        const size_t encodedLen = RleEncode(content, content, rawLen);
        inst.sendBuf[1 + encodedLen] = '#';
        head = encodedLen + 2;

        uint8_t checksum = 0;
        for (size_t i = 1; i < head - 1; i++)
            checksum += inst.sendBuf[i];

        inst.sendBuf[head] = ToHex(checksum >> 4);
        inst.sendBuf[head + 1] = ToHex(checksum & 0xF);

        return RawSend(inst, RoBuffer(inst.sendBuf, head + 2));
    }

    static NpkStatus SendOk(GdbData& inst)
    {
        inst.sendBuf[0] = '$';
        inst.sendBuf[1] = 'O';
        inst.sendBuf[2] = 'K';
        inst.sendBuf[3] = '#';

        return FlushPacket(inst, 4);
    }

    static NpkStatus SendUnsupported(GdbData& inst)
    {
        inst.sendBuf[0] = '$';
        inst.sendBuf[1] = '#';

        return FlushPacket(inst, 2);
    }

    static NpkStatus SendError(GdbData& inst, GdbError which)
    {
        const uint8_t v = static_cast<uint8_t>(which);

        size_t head = 0;
        inst.sendBuf[head++] = '$';
        inst.sendBuf[head++] = 'E';
        inst.sendBuf[head++] = ToHex(v >> 4);
        inst.sendBuf[head++] = ToHex(v & 0xF);
        inst.sendBuf[head++] = '#';

        return FlushPacket(inst, head);
    }

    //returns the number of data bytes (not including delims '$' and '#'),
    //the receive buffer starts with packet data, not '$'.
    static size_t ReceivePacket(GdbData& inst)
    {
        size_t total = 0;
        bool hasStart = false;

        while (true)
        {
            size_t space = RecvBufSize - total;
            if (space == 0)
            {
                total = 0;
                hasStart = false;
                continue;
            }

            size_t got = inst.transport->Receive(inst.transport,
                RwBuffer(inst.recvBuf + total, space));
            if (got == 0)
            {
                sl::HintSpinloop();
                continue;
            }

            if (!hasStart)
            {
                constexpr size_t NoStart = (size_t)-1;

                //scan for the start of a packet
                size_t start = NoStart;
                for (size_t i = 0; i < got; i++)
                {
                    if (inst.recvBuf[i + total] == '$')
                    {
                        start = i + total;
                        break;
                    }
                }
                if (start == NoStart)
                    continue;

                //found a packet, trim leading junk so buffer starts with packet
                //contents.
                size_t remaining = got - (start - total);
                sl::MemMove(inst.recvBuf, inst.recvBuf + start, remaining);
                total = remaining;
                hasStart = true;
            }
            else
                total += got;


            for (size_t i = 1; i < total; i++)
            {
                if (inst.recvBuf[i] != '#')
                    continue;
                if (i + 2 >= total)
                    break;

                //found the end delim, look for a checksum
                uint8_t expected = 0;
                for (size_t j = 1; j < i; j++)
                    expected += inst.recvBuf[j];

                const uint8_t checksum = (FromHex(inst.recvBuf[i + 1]) << 4) 
                    | FromHex(inst.recvBuf[i + 2]);

                if (!inst.noAck)
                {
                    uint8_t ack = '+';
                    if (checksum != expected)
                        ack = '-';

                    inst.transport->Transmit(inst.transport, { &ack, 1 });
                    if (ack == '-')
                    {
                        total = 0;
                        hasStart = false;

                        break;
                    }
                }

                //check for any RLE/escape encode done by the host
                bool hasEncoding = false;
                for (size_t k = 1; k < i && !hasEncoding; k++)
                {
                    const uint8_t b = inst.recvBuf[k];
                    hasEncoding = (b == '*' || b == GdbRleEscape);
                }

                const size_t rawLen = i - 1;
                if (!hasEncoding)
                    return rawLen; //fast path, nothing fancy going on

                //slow path: decode incoming data in-place.
                auto* staging = inst.recvBuf + RecvBufSize / 2;
                sl::MemMove(staging, inst.recvBuf + 1, rawLen);
                const size_t expandedLen = RleDecode(inst.recvBuf + 1, 
                    RecvBufSize / 2 - 1, staging, rawLen);

                return expandedLen;
            }
        }
    }

    //returns the number of bytes needed to represent a register. If the
    //register exists on the current hardware the length returned is based on
    //the register's true size, otherwise it is based on the maximum supported
    //length reported by the `GdbArchRegs` table.
    static size_t CountRegBytes(const GdbArchReg& reg)
    {
        if (reg.valid)
        {
            const size_t width = HwGetRegisterWidth(reg.hwReg);
            if (width == 0)
                return sl::AlignUp(reg.bits, 8) / 8;

            if (width > MaxRegBytes)
            {
                DebuggerPanic("HwGetRegisterWidth() for %zu (%s) returned %zu, "
                    "which is larger than the max of %zu", reg.hwReg, reg.name,
                    width, MaxRegBytes);
            }

            return width;
        }

        return sl::AlignUp(reg.bits, 8) / 8;
    }

    //returns the length written to buffer, or 0 if an error occured.
    static size_t GenerateXml(char* buffer, size_t bufSize)
    {
        size_t head = 0;

        auto advance = sl::SnPrintf(buffer + head, bufSize - head,
            "<?xml version=\"1.0\"?>\n"
            "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">\n"
            "<target version=\"1.0\">\n"
            "  <architecture>%s</architecture>\n"
            "  <feature name=\"%s\">\n",
            GdbArchName, GdbArchFeature);

        if (advance == 0)
            return 0;
        head += static_cast<size_t>(advance);

        advance = sl::SnPrintf(buffer + head, bufSize - head, "%s", 
            GdbArchHeader);
        if (advance == 0)
            return 0;
        head += static_cast<size_t>(advance);

        for (size_t i = 0; i < GdbArchRegCount; i++)
        {
            const auto& reg = GdbArchRegs[i];

            advance = sl::SnPrintf(buffer + head, bufSize - head,
                "    <reg name=\"%s\" bitsize=\"%zu\" type=\"%s\"/>\n",
                reg.name, CountRegBytes(reg) * 8, reg.gdbType);

            if (advance == 0)
                return 0;
            head += static_cast<size_t>(advance);
        }

        advance = sl::SnPrintf(buffer + head, bufSize - head,
            "  </feature>\n"
            "</target>\n");

        if (advance == 0)
            return 0;
        head += static_cast<size_t>(advance);

        return head;
    }

    //returns number of character written to `buffer`, or 0 if an error occured.
    static size_t ReadRegister(RwBuffer buffer, size_t regNum, TrapFrame* frame)
    {
        if (regNum >= GdbArchRegCount)
            return 0;

        const auto& reg = GdbArchRegs[regNum];
        const size_t bytes = CountRegBytes(reg);

        if (buffer.Size() < bytes * 2)
            return 0;

        if (!reg.valid || bytes > MaxRegBytes || frame == nullptr)
        {
            sl::MemSet(buffer.Begin(), 'x', bytes * 2);

            return bytes * 2;
        }

        uint8_t store[MaxRegBytes];
        sl::MemSet(store, 0, MaxRegBytes);

        auto result = HwAccessRegister(*frame, reg.hwReg, nullptr,
            { store, bytes }, false);
        if (result != NpkStatus::Success)
        {
            sl::MemSet(buffer.Begin(), 'x', bytes * 2);

            return bytes * 2;
        }

        for (size_t i = 0; i < bytes; i++)
        {
            buffer[i * 2] = ToHex(store[i] >> 4);
            buffer[i * 2 + 1] = ToHex(store[i] & 0xF);
        }

        return bytes * 2;
    }

    static NpkStatus WriteRegister(size_t regNum, TrapFrame* frame, 
        RoBuffer buffer)
    {
        if (regNum >= GdbArchRegCount || frame == nullptr)
            return NpkStatus::InvalidArg;

        const auto& reg = GdbArchRegs[regNum];

        if (!reg.valid)
            return NpkStatus::InvalidArg;

        const size_t bytes = HwGetRegisterWidth(reg.hwReg);
        if (bytes == 0 || bytes > MaxRegBytes)
            return NpkStatus::InvalidArg;
        if (buffer.Size() < bytes * 2)
            return NpkStatus::InvalidArg;

        for (size_t i = 0; i < bytes * 2; i++)
        {
            if (!IsHexDigit(buffer[i]))
                return NpkStatus::InvalidArg;
        }

        uint8_t store[MaxRegBytes];
        for (size_t i = 0; i < bytes; i++)
        {
            store[i] = FromHex(buffer[i * 2]) << 4;
            store[i] |= FromHex(buffer[i * 2 + 1]);
        }

        return HwAccessRegister(*frame, reg.hwReg, nullptr,
            RwBuffer(store, bytes), true);
    }

    static TrapFrame* GetThreadFrame(GdbData& inst, CpuId who)
    {
        if (who == AnyThread || who == AllThreads)
            who = MyCoreId();

        if (who == MyCoreId())
            return inst.stopFrame;

        if (who >= GetDebugCpuCount())
            return nullptr;

        return DebugFrameForCpu(who);
    }

    static NpkStatus SendStopReason(GdbData& inst)
    {
        size_t head = 0;

        inst.sendBuf[head++] = '$';
        head += (size_t)sl::SnPrintf((char*)inst.sendBuf + head,
            SendBufSize - head, "T%02x", inst.stopSignal);
        head += (size_t)sl::SnPrintf((char*)inst.sendBuf + head,
            SendBufSize - head, "thread:");
        head += PutThreadId({ inst.sendBuf + head, SendBufSize - head },
            MyCoreId());
        inst.sendBuf[head++] = ';';

        if (inst.stopBreakpoint != nullptr && 
            inst.stopBreakpointType == BreakpointType::Breakpoint)
        {
            const auto* bp = inst.stopBreakpoint;

            if (bp->execute)
            {
                if (inst.peerFeatures.hwBreak && bp->hardware)
                {
                    head += (size_t)sl::SnPrintf((char*)inst.sendBuf + head,
                        SendBufSize - head, "hwbreak:;");
                }
                else if (inst.peerFeatures.swBreak && !bp->hardware)
                {
                    head += (size_t)sl::SnPrintf((char*)inst.sendBuf + head,
                        SendBufSize - head, "swbreak:;");
                }
            }

            const uint8_t x = (uint8_t)bp->read | ((uint8_t)bp->write << 1);
            switch (x)
            {
            case 0b01:
                head += (size_t)sl::SnPrintf((char*)inst.sendBuf + head,
                    SendBufSize - head, "rwatch:%zx;", bp->addr);
                break;

            case 0b10:
                head += (size_t)sl::SnPrintf((char*)inst.sendBuf + head,
                    SendBufSize - head, "watch:%zx;", bp->addr);
                break;

            case 0b11:
                head += (size_t)sl::SnPrintf((char*)inst.sendBuf + head,
                    SendBufSize - head, "awatch:%zx;", bp->addr);
                break;
            }
        }
        inst.sendBuf[head++] = '#';

        return FlushPacket(inst, head);
    }

    static NpkStatus SendThreadInfo(GdbData& inst)
    {
        const CpuId count = GetDebugCpuCount();
        bool first = true;
        size_t head = 0;

        inst.sendBuf[head++] = '$';
        inst.sendBuf[head++] = 'm';

        for (CpuId i = inst.threadListHead; i < count; i++)
        {
            if (!first)
                inst.sendBuf[head++] = ',';
            first = false;
            head += PutThreadId(
                RwBuffer(inst.sendBuf + head,
                    SendBufSize - head - 3),
                i);
            inst.threadListHead = i + 1;

            //see if there's enough space for another thread item + packet tail
            if (head + 24 > SendBufSize - 3)
                break;
        }

        inst.sendBuf[head++] = '#';

        return FlushPacket(inst, head);
    }

    static NpkStatus ExtractZPacketArgs(GdbBreakpointArgs& args, RoBuffer buff)
    {
        if (buff.Size() < 2)
            return NpkStatus::InvalidArg;
        if (buff[0] != 'z' && buff[0] != 'Z')
            return NpkStatus::InvalidArg;

        args.type = buff[1] - '0';
        buff = buff.Subspan(2, -1);
        if (buff.Empty() || buff[0] != ',')
            return NpkStatus::InvalidArg;

        buff = buff.Subspan(1, -1);
        const size_t count = ParseHexInt(args.address, buff);
        if (count == 0 || buff.Size() <= count || buff[count] != ',')
            return NpkStatus::InvalidArg;

        buff = buff.Subspan(count + 1, -1);
        ParseHexInt(args.kind, buff);

        return NpkStatus::Success;
    }

    struct GdbCommand
    {
        sl::StringSpan prefix;
        NpkStatus (*Execute)(GdbData& inst, RoBuffer packet);
    };

    static const GdbCommand GdbCommands[] =
    {
        {
            "qXfer:features:read:target.xml:"_span,
            [](GdbData& inst, RoBuffer packet) -> NpkStatus
            {
                if (GdbArchRegCount == 0)
                    return SendUnsupported(inst);

                const size_t prefixLen = 
                    sizeof("qXfer:features:read:target.xml:") - 1;
                packet = packet.Subspan(prefixLen, -1);

                uintptr_t offset = 0;
                const size_t len = ParseHexInt(offset, packet);
                if (len == 0 || packet.Size() <= len || packet[len] != ',')
                    return SendError(inst, GdbError::InvalidArg);

                uintptr_t length = 0;
                packet = packet.Subspan(len + 1, -1);
                ParseHexInt(length, packet);

                //... XML? who thought this was a good solution to the problem?
                //Now I have to generate XML in a kernel, I'm sure this will
                //have no long term issues.
                //Anyway, GenerateXml() outputs directly into the buffer we give
                //it, so we reserve space for '$' and 'm'/'l' before the xml
                //data, and another 3 bytes after for '#' and the checksum.
                const size_t xmlLen = GenerateXml(
                    (char*)inst.sendBuf + 2, SendBufSize - 5);

                if (offset > xmlLen)
                    offset = xmlLen;

                const size_t avail = xmlLen - offset;
                const size_t chunk = sl::Min(length, (uintptr_t)avail);
                const bool last  = (offset + chunk >= xmlLen);

                inst.sendBuf[0] = '$';
                inst.sendBuf[1] = last ? 'l' : 'm';

                if (offset > 0)
                {
                    sl::MemMove(inst.sendBuf + 2, inst.sendBuf + 2 + offset, 
                        chunk);
                }

                size_t head = 2 + chunk;
                inst.sendBuf[head++] = '#';

                return FlushPacket(inst, head);
            }
        },
        {
            "qSupported"_span,
            [](GdbData& inst, RoBuffer packet) -> NpkStatus
            {
                inst.peerFeatures.swBreak = packet.Contains("swbreak+"_u8span);
                inst.peerFeatures.hwBreak = packet.Contains("hwbreak+"_u8span);
                inst.peerFeatures.errorMsg =
                    packet.Contains("error-message+"_u8span);
                inst.peerFeatures.xmlFeatures =
                    packet.Contains("xmlRegisters=i386"_u8span)
                    || packet.Contains("qXfer:features:read+"_u8span);

                //due to how runlength decoding uses the higher half of RecvBuf,
                //we can only advertise half it's size, minus one.
                const size_t packetSize = RecvBufSize / 2 - 1;

                const size_t len = (size_t)sl::SnPrintf(
                    (char*)inst.sendBuf, SendBufSize,
                    "$PacketSize=%zx;"
                    "hwbreak+;"
                    "swbreak+;"
                    "error-message+;"
                    "vContSupported+;"
                    "qXfer:features:read+;"
                    "QStartNoAckMode+"
                    "#",
                    packetSize);

                return FlushPacket(inst, len);
            }
        },
        {
            "QStartNoAckMode"_span,
            [](GdbData& inst, RoBuffer packet) -> NpkStatus
            {
                (void)packet;

                auto result = SendOk(inst);
                if (result != NpkStatus::Success)
                    return result;

                inst.noAck = true;

                return NpkStatus::Success;
            }
        },
        {
            "qAttached"_span,
            [](GdbData& inst, RoBuffer packet) -> NpkStatus
            {
                (void)packet;

                //return '1', meaning attached to existing 'process'
                inst.sendBuf[0] = '$';
                inst.sendBuf[1] = '1';
                inst.sendBuf[2] = '#';

                return FlushPacket(inst, 3);
            }
        },
        {
            "qC"_span,
            [](GdbData& inst, RoBuffer packet) -> NpkStatus
            {
                (void)packet;

                size_t head = 0;
                inst.sendBuf[head++] = '$';
                inst.sendBuf[head++] = 'Q';
                inst.sendBuf[head++] = 'C';
                head += PutThreadId(
                    RwBuffer(inst.sendBuf + head, SendBufSize - head),
                    MyCoreId());
                inst.sendBuf[head++] = '#';

                return FlushPacket(inst, head);
            }
        },
        {
            "qfThreadInfo"_span,
            [](GdbData& inst, RoBuffer packet) -> NpkStatus
            {
                (void)packet;

                inst.threadListHead = 0;

                return SendThreadInfo(inst);
            }
        },
        {
            "qsThreadInfo"_span,
            [](GdbData& inst, RoBuffer packet) -> NpkStatus
            {
                (void)packet;

                if (inst.threadListHead < GetDebugCpuCount())
                    return SendThreadInfo(inst);

                inst.sendBuf[0] = '$';
                inst.sendBuf[1] = 'l';
                inst.sendBuf[2] = '#';

                return FlushPacket(inst, 3);
            }
        },
        {
            "qThreadExtraInfo,"_span,
            [](GdbData& inst, RoBuffer packet) -> NpkStatus
            {
                const size_t prefix = sizeof("qThreadExtraInfo,") - 1;

                CpuId id;
                ParseThreadId(id, packet.Subspan(prefix, -1));
                if (id == AnyThread || id == AllThreads)
                    id = MyCoreId();

                //TODO: display more info about the thread/cpu here
                char store[32];
                size_t storeLen = (size_t)sl::SnPrintf(store, sizeof(store),
                    "core %zu%s", (size_t)id, id == 0 ? ", bsp" : "");

                size_t head = 0;
                inst.sendBuf[head++] = '$';
                for (size_t i = 0; i < storeLen; i++)
                {
                    inst.sendBuf[head++] = ToHex((uint8_t)store[i] >> 4);
                    inst.sendBuf[head++] = ToHex((uint8_t)store[i] & 0xF);
                }
                inst.sendBuf[head++] = '#';

                return FlushPacket(inst, head);
            }
        },
        {
            "?"_span,
            [](GdbData& inst, RoBuffer packet) -> NpkStatus
            {
                (void)packet;

                return SendStopReason(inst);
            }
        },
        {
            "H"_span,
            [](GdbData& inst, RoBuffer packet) -> NpkStatus
            {
                if (packet.Size() < 2)
                    return SendError(inst, GdbError::MissingArg);

                CpuId tid;
                ParseThreadId(tid, packet.Subspan(2, packet.Size() - 2));

                if (tid == AnyThread)
                    tid = MyCoreId();

                const uint8_t op = packet[1];
                switch (op)
                {
                case 'g':
                    inst.selectedThread = tid;
                    return SendOk(inst);

                case 'c':
                    inst.continueThread = tid;
                    return SendOk(inst);

                default:
                    return SendError(inst, GdbError::InvalidArg);
                }
            }
        },
        {
            "T"_span,
            [](GdbData& inst, RoBuffer packet) -> NpkStatus
            {
                CpuId tid;
                ParseThreadId(tid, packet.Subspan(1, packet.Size() - 1));

                if (tid == AnyThread || tid == AllThreads)
                    return SendOk(inst);
                if (tid >= GetDebugCpuCount())
                    return SendError(inst, GdbError::InvalidArg);

                return SendOk(inst);
            }
        },
        {
            "g"_span,
            [](GdbData& inst, RoBuffer packet) -> NpkStatus
            {
                (void)packet;

                TrapFrame* frame = GetThreadFrame(inst, inst.selectedThread);
                size_t head = 0;

                inst.sendBuf[head++] = '$';
                for (size_t i = 0; i < GdbArchRegCount; i++)
                {
                    const auto buff = 
                        RwBuffer(inst.sendBuf + head, SendBufSize - head - 3);

                    const size_t len = ReadRegister(buff, i, frame);
                    if (len == 0)
                        return SendError(inst, GdbError::InternalError);

                    head += len;
                }
                inst.sendBuf[head++] = '#';

                return FlushPacket(inst, head);
            }
        },
        {
            "G"_span,
            [](GdbData& inst, RoBuffer packet) -> NpkStatus
            {
                TrapFrame* frame = GetThreadFrame(inst, inst.selectedThread);

                auto data = packet.Subspan(1, packet.Size() - 1);
                size_t head = 0;

                for (size_t i = 0; i < GdbArchRegCount; i++)
                {
                    const size_t bytes = CountRegBytes(GdbArchRegs[i]);
                    if (data.Size() - head < bytes * 2)
                        break;

                    //we silently ignore failures to write to registers, this
                    //is apparently fine?
                    const auto value = data.Subspan(head, data.Size() - head);
                    WriteRegister(i, frame, value);
                    head += bytes * 2;
                }

                return SendOk(inst);
            }
        },
        {
            "p"_span,
            [](GdbData& inst, RoBuffer packet) -> NpkStatus
            {
                uintptr_t regNum;
                ParseHexInt(regNum, packet.Subspan(1, packet.Size() - 1));
                if (regNum >= GdbArchRegCount)
                    return SendError(inst, GdbError::InvalidArg);

                TrapFrame* frame = GetThreadFrame(inst, inst.selectedThread);
                size_t head = 0;

                inst.sendBuf[head++] = '$';
                const size_t count = ReadRegister(
                    RwBuffer(inst.sendBuf + head, SendBufSize - head - 3),
                    (size_t)regNum, frame);
                if (count == 0)
                    return SendError(inst, GdbError::InternalError);

                head += count;
                inst.sendBuf[head++] = '#';

                return FlushPacket(inst, head);
            }
        },
        {
            "P"_span,
            [](GdbData& inst, RoBuffer packet) -> NpkStatus
            {
                packet = packet.Subspan(1, -1);

                uintptr_t regNum;
                const size_t count = ParseHexInt(regNum, packet);
                if (count == 0 || packet.Size() <= count 
                    || packet[count] != '=')
                    return SendError(inst, GdbError::MissingArg);
                if (regNum >= GdbArchRegCount)
                    return SendError(inst, GdbError::InvalidArg);

                TrapFrame* frame = GetThreadFrame(inst, inst.selectedThread);
                if (frame == nullptr)
                    return SendError(inst, GdbError::InternalError);

                const auto valueLen = packet.Size() - count - 1;
                auto value = packet.Subspan(count + 1, valueLen);
                auto result = WriteRegister((size_t)regNum, frame, value); 
                if (result != NpkStatus::Success)
                    return SendError(inst, GdbError::InvalidArg);

                return SendOk(inst);
            }
        },
        {
            "m"_span,
            [](GdbData& inst, RoBuffer packet) -> NpkStatus
            {
                packet  = packet.Subspan(1, packet.Size() - 1);

                uintptr_t addr;
                const size_t count = ParseHexInt(addr, packet);
                if (count == 0 || packet.Size() <= count || packet[count] !=',')
                    return SendError(inst, GdbError::MissingArg);

                uintptr_t length;
                packet = packet.Subspan(count + 1, -1);
                ParseHexInt(length, packet);

                if (length == 0)
                    return SendUnsupported(inst);

                const size_t maxBytes = (SendBufSize - 4) / 2;
                if (length > maxBytes)
                    length = maxBytes;

                //We use the upper half of the send buffer as scratch space to
                //store the read memory into, then encode it into the lower
                //half. The encoded data takes twice as much space but this is
                //fine as we have a local copy of the byte over writing its
                //conversion to the buffer. This allows us to skip using an
                //extra buffer.
                auto store = reinterpret_cast<uint8_t*>(
                    inst.sendBuf + SendBufSize / 2);
                const bool abort = MemCopyExceptionAware(store, 
                    reinterpret_cast<const void*>(addr), length);

                if (abort)
                    return SendError(inst, GdbError::BadAddress);

                size_t head = 0;
                inst.sendBuf[head++] = '$';
                for (size_t i = 0; i < (size_t)length; i++)
                {
                    const auto value = store[i];

                    inst.sendBuf[head++] = ToHex(value >> 4);
                    inst.sendBuf[head++] = ToHex(value & 0xF);
                }
                inst.sendBuf[head++] = '#';

                return FlushPacket(inst, head);
            }
        },
        {
            "M"_span,
            [](GdbData& inst, RoBuffer packet) -> NpkStatus
            {
                packet = packet.Subspan(1, packet.Size() - 1);

                uintptr_t addr;
                const size_t count = ParseHexInt(addr, packet);
                if (count == 0 || packet.Size() <= count || packet[count] !=',')
                    return SendError(inst, GdbError::MissingArg);

                uintptr_t length;
                packet = packet.Subspan(count + 1, -1);
                const size_t count2 = ParseHexInt(length, packet);
                if (count2 == 0)
                    return SendError(inst, GdbError::MissingArg);

                const size_t colon = count2;
                if (packet.Size() <= colon || packet[colon] != ':')
                    return SendError(inst, GdbError::MissingArg);

                packet = packet.Subspan(colon + 1, -1);

                if (length == 0)
                    return SendOk(inst);
                if (packet.Size() < length * 2)
                    return SendError(inst, GdbError::InvalidArg);

                auto store = reinterpret_cast<uint8_t*>(
                    inst.sendBuf + SendBufSize / 2);
                for (size_t i = 0; i < length; i++)
                {
                    const size_t l = i * 2;

                    if (!IsHexDigit(packet[l]) || !IsHexDigit(packet[l + 1]))
                        return SendError(inst, GdbError::InvalidArg);

                    store[i] = FromHex(packet[l]) << 4;
                    store[i] |= FromHex(packet[l + 1]);
                }

                //TODO: do we want to arch-specific hooks here to make memory
                //writable even when it normally wouldn't be?
                const bool abort = MemCopyExceptionAware(
                    reinterpret_cast<void*>(addr), store, length);
                if (abort)
                    return SendError(inst, GdbError::BadAddress);

                return SendOk(inst);
            }
        },
        {
            "Z"_span,
            [](GdbData& inst, RoBuffer packet) -> NpkStatus
            {
                GdbBreakpointArgs args;
                auto result = ExtractZPacketArgs(args, packet);

                if (result != NpkStatus::Success)
                    return SendError(inst, GdbError::InvalidArg);

                if (args.type >= TemplateCount)
                    return SendUnsupported(inst);

                Breakpoint* bp = AllocBreakpoint();
                if (bp == nullptr)
                    return SendError(inst, GdbError::InternalError);

                *bp = GdbBreakpointTemplates[args.type];
                bp->addr = args.address;
                bp->kind = args.kind;

                if (!ArmBreakpoint(*bp))
                {
                    FreeBreakpoint(&bp);
                    return SendError(inst, GdbError::InternalError);
                }

                return SendOk(inst);
            }
        },
        {
            "z"_span,
            [](GdbData& inst, RoBuffer packet) -> NpkStatus
            {
                GdbBreakpointArgs args;
                auto result = ExtractZPacketArgs(args, packet);
                if (result != NpkStatus::Success)
                    return SendError(inst, GdbError::InvalidArg);

                //TODO: we should only remove breakpoints matching the type 
                //+ kind fields.
                Breakpoint* bp = GetBreakpointByAddr(args.address);
                if (bp == nullptr)
                    return SendError(inst, GdbError::InvalidArg);

                if (!DisarmBreakpoint(*bp))
                    return SendError(inst, GdbError::InternalError);

                FreeBreakpoint(&bp);
                return SendOk(inst);
            }
        },
        {
            "vCont?"_span,
            [](GdbData& inst, RoBuffer packet) -> NpkStatus
            {
                (void)packet;

                const size_t len = (size_t)sl::SnPrintf((char*)inst.sendBuf,
                    SendBufSize, "$vCont;c;C;s;S;r#");

                return FlushPacket(inst, len);
            }
        },
        {
            "vCont;"_span,
            [](GdbData& inst, RoBuffer packet) -> NpkStatus
            {
                constexpr uintptr_t StepBit = 1;
                constexpr uintptr_t ExplicitBit = 2;
                constexpr size_t pfxLen = sizeof("vCont;") - 1;

                packet = packet.Subspan(pfxLen, -1);

                const CpuId cpuCount = GetDebugCpuCount();
                auto stores = GetCpuDebugStores();
                for (size_t i = 0; i < stores.Size(); i++)
                    stores[i] = 0;

                bool defaultStep = false;
                bool defaultIsRange = false;
                uintptr_t defaultRangeStart = 0;
                uintptr_t defaultRangeEnd = 0;
                bool hasAction = false;
                bool hasDefault = false;

                while (!packet.Empty())
                {
                    uint8_t action = packet[0];
                    packet = packet.Subspan(1, -1);

                    //treat 'C'/'S' packets as 'c'/'s' since we have no signals
                    //in the kernel.
                    if (action == 'C' || action == 'S')
                    {
                        while (!packet.Empty() && IsHexDigit(packet[0]))
                            packet = packet.Subspan(1, -1);

                        if (action == 'C')
                            action = 'c';
                        else
                            action = 's';
                    }

                    uintptr_t rangeStart = 0;
                    uintptr_t rangeEnd = 0;
                    if (action == 'r')
                    {
                        size_t count = ParseHexInt(rangeStart, packet);
                        packet = packet.Subspan(count, -1);

                        if (!packet.Empty() && packet[0] == ',')
                        {
                            packet = packet.Subspan(1, -1);
                            count = ParseHexInt(rangeEnd, packet);
                            packet = packet.Subspan(count, -1);
                        }
                    }

                    //determine if this action is a default or applies to a
                    //specific thread (which thread then?).
                    CpuId tid = AllThreads;
                    if (!packet.Empty() && packet[0] == ':')
                    {
                        packet = packet.Subspan(1, -1);
                        const size_t count = ParseThreadId(tid, packet);
                        packet = packet.Subspan(count, -1);
                    }

                    if (!packet.Empty() && packet[0] == ';')
                        packet = packet.Subspan(1, -1);

                    const bool doStep = (action == 's' || action == 'r');
                    hasAction = true;

                    if (tid == AllThreads || tid == AnyThread)
                    {
                        if (!hasDefault)
                        {
                            hasDefault = true;
                            defaultStep = doStep;

                            if (action == 'r')
                            {
                                defaultIsRange = true;
                                defaultRangeStart = rangeStart;
                                defaultRangeEnd = rangeEnd;
                            }
                        }
                    }
                    else if (tid < cpuCount)
                    {
                        auto* baseStore = &stores[tid * PerCpuStorePointers];

                        auto& flags = baseStore[CpuStoreFlags];
                        if ((flags & ExplicitBit) != 0)
                            continue;
                        flags = ExplicitBit;
                        if (doStep)
                            flags |= StepBit;

                        if (action == 'r')
                        {
                            baseStore[CpuStoreRangeStart] = rangeStart;
                            baseStore[CpuStoreRangeEnd] = rangeEnd;
                        }
                    }
                }

                if (!hasAction)
                    return SendError(inst, GdbError::MissingArg);

                for (CpuId i = 0; i < cpuCount; i++)
                {
                    auto* baseStore = &stores[i * PerCpuStorePointers];

                    const auto flags = baseStore[CpuStoreFlags];
                    bool doStep = defaultStep;
                    if (flags & ExplicitBit)
                        doStep = (flags & StepBit) != 0;
                    else if (defaultIsRange)
                    {
                        baseStore[CpuStoreRangeStart] = defaultRangeStart;
                        baseStore[CpuStoreRangeEnd] = defaultRangeEnd;
                    }

                    if (!doStep)
                        continue;

                    auto* frame = GetThreadFrame(inst, i);
                    if (frame != nullptr)
                        HwSetSingleStep(*frame, true);
                }

                inst.breakCommandLoop = true;

                return NpkStatus::Success;
            }
        },
        {
            "c"_span,
            [](GdbData& inst, RoBuffer packet) -> NpkStatus
            {
                (void)packet;

                inst.breakCommandLoop = true;

                return NpkStatus::Success;
            }
        },
        {
            "C"_span,
            [](GdbData& inst, RoBuffer packet) -> NpkStatus
            {
                (void)packet;

                inst.breakCommandLoop = true;

                return NpkStatus::Success;
            }
        },
        {
            "s"_span,
            [](GdbData& inst, RoBuffer packet) -> NpkStatus
            {
                (void)packet;

                auto* frame = GetThreadFrame(inst, inst.continueThread);
                if (frame != nullptr)
                    HwSetSingleStep(*frame, true);
                inst.breakCommandLoop = true;

                return NpkStatus::Success;
            }
        },
        {
            "S"_span,
            [](GdbData& inst, RoBuffer packet) -> NpkStatus
            {
                (void)packet;

                auto* frame = GetThreadFrame(inst, inst.continueThread);

                if (frame != nullptr)
                    HwSetSingleStep(*frame, true);
                inst.breakCommandLoop = true;

                return NpkStatus::Success;
            }
        },
        {
            "D"_span,
            [](GdbData& inst, RoBuffer packet) -> NpkStatus
            {
                (void)packet;

                SendOk(inst);
                inst.breakCommandLoop = true;
                NotifyOfHostDisconnect();

                return NpkStatus::Success;
            }
        },
        {
            "k"_span,
            [](GdbData& inst, RoBuffer packet) -> NpkStatus
            {
                (void)packet;

                inst.breakCommandLoop = true;
                NotifyOfHostDisconnect();

                return NpkStatus::Success;
            }
        },
    };

    static NpkStatus ProcessPacket(GdbData& inst, RoBuffer data)
    {
        constexpr size_t count = sizeof(GdbCommands) / sizeof(GdbCommands[0]);

        for (size_t i = 0; i < count; i++)
        {
            const auto& cmd = GdbCommands[i];

            if (data.Size() < cmd.prefix.Size())
                continue;
            if (sl::MemCompare(data.Begin(), cmd.prefix.Begin(),
                cmd.prefix.Size()) != 0)
                continue;

            return cmd.Execute(inst, data);
        }

        return SendUnsupported(inst);
    }

    static void DoCommandLoop(GdbData& inst)
    {
        inst.breakCommandLoop = false;

        while (!inst.breakCommandLoop)
        {
            const size_t recvLen = ReceivePacket(inst);
            if (recvLen == 0)
                continue;

            ProcessPacket(inst, { inst.recvBuf + 1, recvLen });
        }
    }

    static NpkStatus GdbConnect(DebugProtocol* proto, DebugTransportList* ports,
        sl::TimeCount timeout)
    {
        auto& gdb = *static_cast<GdbData*>(proto->opaque);

        //TODO: respect timeout
        gdb.selectedThread = (CpuId)MyCoreId();
        gdb.continueThread = AllThreads;
        gdb.threadListHead = 0;
        gdb.stopFrame = IdentityTrapFrame();
        gdb.stopSignal = 5;
        gdb.stopBreakpoint = nullptr;
        gdb.stopBreakpointType = BreakpointType::Manual;
        gdb.noAck = false;
        gdb.breakCommandLoop = false;
        sl::MemSet(&gdb.peerFeatures, 0, sizeof(gdb.peerFeatures));

        for (auto it = ports->Begin(); it != ports->End(); ++it)
        {
            gdb.transport = &*it;

            const size_t dataLen = ReceivePacket(gdb);
            if (dataLen == 0)
            {
                gdb.transport = nullptr;
                continue;
            }

            //we found a transport with someone on the other end, process the
            //first packet manually and then enter the command processor loop.
            ProcessPacket(gdb, { gdb.recvBuf + 1, dataLen });
            DoCommandLoop(gdb);
            gdb.stopFrame = nullptr;

            return NpkStatus::Success;
        }

        gdb.stopFrame = nullptr;
        gdb.transport = nullptr;

        return NpkStatus::NotAvailable;
    }

    static void GdbDisconnect(DebugProtocol* proto)
    {
        ClearAllBreakpoints();

        auto stores = GetCpuDebugStores();
        for (size_t i = 0; i < stores.Size(); i++)
            stores[i] = 0;

        auto& gdb = *static_cast<GdbData*>(proto->opaque);
        gdb.transport = nullptr;
    }

    static NpkStatus GdbBreakpointHit(DebugProtocol* proto, BreakpointType type,
        Breakpoint* bp, TrapFrame* frame)
    {
        auto& gdb = *static_cast<GdbData*>(proto->opaque);

        if (type == BreakpointType::SingleStep)
        {
            HwSetSingleStep(*frame, false);

            const CpuId cpu = MyCoreId();
            auto stores = GetCpuDebugStores();
            auto* store = &stores[cpu * PerCpuStorePointers];

            const uintptr_t rangeEnd = store[CpuStoreRangeEnd];
            if (rangeEnd != 0)
            {
                const uintptr_t pc = GetTrapReturnAddr(frame);
                if (pc >= store[CpuStoreRangeStart] && pc < rangeEnd)
                {
                    HwSetSingleStep(*frame, true);

                    return NpkStatus::Success;
                }

                store[CpuStoreRangeEnd] = 0;
            }
        }

        gdb.stopFrame = frame;
        if (type == BreakpointType::Manual)
            gdb.stopFrame = IdentityTrapFrame();
        gdb.stopSignal = 5;
        gdb.stopBreakpoint = bp;

        SendStopReason(gdb);
        DoCommandLoop(gdb);

        gdb.stopFrame = nullptr;
        gdb.stopBreakpoint = nullptr;

        return NpkStatus::Success;
    }

    DebugProtocol gdbProtocol
    {
        .name = "gdb-remote",
        .opaque = &gdbInst,
        .Connect = GdbConnect,
        .Disconnect = GdbDisconnect,
        .BreakpointHit = GdbBreakpointHit,
    };
}
