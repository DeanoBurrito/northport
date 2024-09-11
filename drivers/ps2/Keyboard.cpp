#include <Keyboard.h>
#include <Controller.h>
#include <ScancodeTables.h>
#include <Log.h>
#include <ApiStrings.h>
#include <NanoPrintf.h>
#include <interfaces/driver/Interrupts.h>
#include <interfaces/driver/Drivers.h>

namespace Ps2
{
    constexpr size_t WorkingBuffSize = 0x10; //longest we should get is 8 (set 2, pause pressed)
    uint8_t workingBuff[WorkingBuffSize];
    size_t workingBuffHead;

    bool ctrlDown;
    bool altDown;
    bool deleteDown;
    bool nextIsMagicKey;

    npk_interrupt_route kbIntrRoute;
    npk_dpc kbDpc;
    npk_io_device_api kbApi;

    static bool TryParseScancode()
    {
        if (workingBuffHead == 0)
            return false;

        //TODO: pause key edge case (0xe1)
        const bool extended = workingBuff[0] == 0xE0;
        size_t buffHead = extended ? 1 : 0;
        if (buffHead == workingBuffHead)
            return false;

        const bool released = workingBuff[buffHead] == 0xF0;
        buffHead += released ? 1 : 0;
        if (buffHead == workingBuffHead)
            return false;

        sl::Span<uint8_t> buff(workingBuff + buffHead, workingBuffHead - buffHead);
        auto parsed = extended ? TryParseSet2Extended(buff) : TryParseSet2Base(buff);
        if (!parsed.HasValue())
            return false;

        //magic key handling
        if (nextIsMagicKey && !released)
        {
            npk_send_magic_key(*parsed);
            nextIsMagicKey = false;
        }
        switch (*parsed)
        {
        case npk_key_id_lctrl:
            ctrlDown = !released;
            break;
        case npk_key_id_lalt:
            altDown = !released;
            break;
        case npk_key_id_delete:
            deleteDown = !released;
            break;
        default: 
            break;
        }
        if (ctrlDown && altDown && deleteDown)
            nextIsMagicKey = true;

        //TODO: if there are any queued iops for this device, create an npk_input_event and add it
        //to their buffer.
        Log("Key parsed, dont forget to send this to an iop!", LogLevel::Debug);

        return true;
    }

    static void KeyboardDpc(void* unused)
    {
        (void)unused;

        workingBuff[workingBuffHead++] = *ReadByte(true);
        if (TryParseScancode())
            workingBuffHead = 0;

        if (workingBuffHead == WorkingBuffSize)
        {
            char printBuff[WorkingBuffSize * 3];
            size_t printBuffHead = 0;
            for (size_t i = 0; i < WorkingBuffSize; i++)
            {
                printBuffHead += npf_snprintf(printBuff + printBuffHead, 
                    (WorkingBuffSize * 3) - printBuffHead, "%02x ", workingBuff[i]);
            }

            Log("Dropping buffer contents, unknown scancode: %.*s", LogLevel::Warning,
                (int)printBuffHead, printBuff);
            workingBuffHead = 0;
        }
    }

    static npk_string GetSummary(npk_device_api* api)
    { return "ps/2 keyboard"_apistr; }

    static bool BeginKeyboardOp(npk_device_api* api, npk_iop_context* context, npk_iop_frame* frame)
    { return false; }

    static bool EndKeyboardOp(npk_device_api* api, npk_iop_context* context, npk_iop_frame* frame)
    { return false; }

    static bool ResetKeyboard()
    { return true; }

    bool InitKeyboard(npk_event_add_device* event)
    {
        Log("Initializing keyboard.", LogLevel::Verbose);
        VALIDATE_(ResetKeyboard(), false);

        //TODO: set scancode set 2, do self test

        ctrlDown = altDown = deleteDown = false;
        nextIsMagicKey = false;
        workingBuffHead = 0;
        kbIntrRoute.callback = nullptr;
        kbIntrRoute.dpc = &kbDpc;
        kbDpc.function = KeyboardDpc;

        const size_t irqNum = GetKeyboardIrqNum();
        VALIDATE_(npk_claim_interrupt_route(&kbIntrRoute, NPK_NO_AFFINITY, irqNum), false);
        Log("Keyboard interrupt on gsi-%zu", LogLevel::Verbose, irqNum);
        EnableInterrupts(false, true);

        kbApi.header.type = npk_device_api_type_io;
        kbApi.header.get_summary = GetSummary;
        kbApi.begin_op = BeginKeyboardOp;
        kbApi.end_op = EndKeyboardOp;
        VALIDATE_(npk_add_device_api(&kbApi.header), false);

        return true;
    }
}
