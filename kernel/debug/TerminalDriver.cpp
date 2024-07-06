#include <debug/TerminalDriver.h>
#include <debug/Terminal.h>
#include <debug/Log.h>
#include <boot/LimineTags.h>
#include <containers/List.h>
#include <UnitConverter.h>
#include <NanoPrintf.h>
#include <tasking/Clock.h>

//these headers are needed to gather the info required for the stats display
#include <debug/BakedConstants.h>
#include <debug/Symbols.h>
#include <memory/Vmm.h>
#include <drivers/DriverManager.h>

namespace Npk::Debug
{
    constexpr const char VersionFormatStr[] = "Kernel v%zu.%zu.%zu (api v%u.%u.%u) uptime=%zu.%03zus";
    constexpr const char VmmFormatStr[] = "[VMM] faults=%zu, anon=%zu.%zu%sB/%zu.%zu%sB (%zu), file=%zu.%zu%sB/%zu.%zu%sB (%zu), mmio=%zu.%zu%sB (%zu)";
    constexpr const char SymsFormatStr[] = "[Syms] pub=%zu priv=%zu other=%zu";
    constexpr const char DriversFormatStr[] = "[Dnd] drivers=%zu/%zu devices=%zu/%zu apis=%zu";
    //TODO: other info to track and display
    //- interrupts per second, and execution time outside of scheduler (int handlers, dpcs)
    //- PMM and file cache usage.
    constexpr const char VersionPreStr[] = "\e[1;1H";
    constexpr const char VmmPreStr[] = "\e[2;1H";
    constexpr const char SymsPreStr[] = "\e[2;91H";
    constexpr const char DriversPreStr[] = "\e[1;44H";

    constexpr size_t StatsFormatBuffSize = 128;
    constexpr const char TermSetPanicPalette[] = "\e[1;1H\e[97;41m";
    constexpr const char StatsSetPanicPalette[] = "\e[1;1H\e[30;101m";

    constexpr GTStyle StatsStyle
    {
        DEFAULT_ANSI_COLOURS,
        DEFAULT_ANSI_BRIGHT_COLOURS,
        0x68000000,
        0xDDDDDD,
        0,
        0
    };

    struct TerminalHead
    {
        TerminalHead* next;

        Terminal logRenderer;
        Terminal statsRenderer;
    };

    sl::IntrFwdList<TerminalHead> termHeads;
    LogOutput termLogOutput;
    bool autoRefreshStarted;
    Tasking::DpcStore refreshStatsDpc;
    Tasking::ClockEvent refreshStatsEvent;

    void UpdateRenderedStats(void*)
    {
        const auto uptime = Tasking::GetUptime();
        const size_t uptimeSeconds = uptime.ToMillis() / 1000;
        const size_t uptimeMillis = uptime.ToMillis() % 1000;
        const size_t verLen = npf_snprintf(nullptr, 0, VersionFormatStr,
            versionMajor, versionMinor, versionRev,
            NP_MODULE_API_VER_MAJOR, NP_MODULE_API_VER_MINOR, NP_MODULE_API_VER_REV,
            uptimeSeconds, uptimeMillis) + 1;

        char verStr[verLen]; //TODO: no VLAs!
        npf_snprintf(verStr, verLen, VersionFormatStr,
            versionMajor, versionMinor, versionRev,
            NP_MODULE_API_VER_MAJOR, NP_MODULE_API_VER_MINOR, NP_MODULE_API_VER_REV,
            uptimeSeconds, uptimeMillis);

        char formatBuff[StatsFormatBuffSize];
        auto vmmStats = VMM::Kernel().GetStats();
        auto anonWss = sl::ConvertUnits(vmmStats.anonWorkingSize, sl::UnitBase::Binary);
        auto anonRss = sl::ConvertUnits(vmmStats.anonResidentSize, sl::UnitBase::Binary);
        auto fileWss = sl::ConvertUnits(vmmStats.fileWorkingSize, sl::UnitBase::Binary);
        auto fileRss = sl::ConvertUnits(vmmStats.fileResidentSize, sl::UnitBase::Binary);
        auto mmioWss = sl::ConvertUnits(vmmStats.mmioWorkingSize, sl::UnitBase::Binary);

        auto symsInfo = GetSymbolStats();
        auto driverInfo = Drivers::DriverManager::Global().GetStats();

        for (auto it = termHeads.Begin(); it != termHeads.End(); it = it->next)
        {
            it->statsRenderer.Write(VersionPreStr, sizeof(VersionPreStr));
            it->statsRenderer.Write(verStr, verLen);

            it->statsRenderer.Write(VmmPreStr, sizeof(VmmPreStr));
            const size_t vmmLen = npf_snprintf(formatBuff, StatsFormatBuffSize, VmmFormatStr, vmmStats.faults,
                anonRss.major, anonRss.minor, anonRss.prefix, anonWss.major, anonWss.minor, anonWss.prefix, vmmStats.anonRanges,
                fileRss.major, fileRss.minor, fileRss.prefix, fileWss.major, fileWss.minor, fileWss.prefix, vmmStats.fileRanges,
                mmioWss.major, mmioWss.minor, mmioWss.prefix, vmmStats.mmioRanges);
            it->statsRenderer.Write(formatBuff, vmmLen);

            it->statsRenderer.Write(SymsPreStr, sizeof(SymsPreStr));
            const size_t symsLen = npf_snprintf(formatBuff, StatsFormatBuffSize, SymsFormatStr,
                symsInfo.publicCount, symsInfo.privateCount, symsInfo.otherCount);
            it->statsRenderer.Write(formatBuff, symsLen);

            it->statsRenderer.Write(DriversPreStr, sizeof(DriversPreStr));
            const size_t dndLen = npf_snprintf(formatBuff, StatsFormatBuffSize, DriversFormatStr,
                driverInfo.loadedCount, driverInfo.manifestCount, driverInfo.totalDescriptors - driverInfo.unclaimedDescriptors, 
                driverInfo.totalDescriptors, driverInfo.apiCount);
            it->statsRenderer.Write(formatBuff, dndLen);
        }

        if (autoRefreshStarted)
        {
            refreshStatsEvent.duration = 10_ms;
            Tasking::QueueClockEvent(&refreshStatsEvent);
        }
    }

    void TerminalWriteCallback(sl::StringSpan text)
    {
        for (auto it = termHeads.Begin(); it != termHeads.End(); it = it->next)
            it->logRenderer.Write(text.Begin(), text.Size());

        if (!autoRefreshStarted)
        {
            if (CoreLocalAvailable() && CoreLocal()[LocalPtr::Thread] != nullptr)
            {
                Log("Starting GTerm stats bar auto-refresh", LogLevel::Info);
                autoRefreshStarted = true;
                Tasking::QueueClockEvent(&refreshStatsEvent);
            }
            else
                UpdateRenderedStats(nullptr);
        }
    }

    void TerminalBeginPanic()
    {
        autoRefreshStarted = false;
        for (auto it = termHeads.Begin(); it != termHeads.End(); it = it->next)
        {
            it->logRenderer.Write(TermSetPanicPalette, sizeof(TermSetPanicPalette));
            it->logRenderer.Clear();
            it->statsRenderer.Write(StatsSetPanicPalette, sizeof(StatsSetPanicPalette));
            UpdateRenderedStats(nullptr);
        }
    }

    void InitEarlyTerminals()
    {
        auto fbResponse = Boot::framebufferRequest.response;
        if (fbResponse == nullptr)
            return;

        autoRefreshStarted = false;
        refreshStatsDpc.data.function = UpdateRenderedStats;
        refreshStatsEvent.duration = 10_ms;
        refreshStatsEvent.callbackCore = NoCoreAffinity;
        refreshStatsEvent.dpc = &refreshStatsDpc;

        //Make full use of all framebuffers reported by the bootloader. We also split each
        //'source' framebuffer into two smaller terminals: the first one occupies most of the screen,
        //and the second one occupies the bottom 2 lines (worth of text). The lower terminal
        //is doesnt scroll and is used to display some cool stats about kernel internals.
        //Thanks to the keyronex for the inspiration for this one.
        auto fontSize = Terminal::CalculateFontSize(DefaultTerminalStyle);
        const size_t reservedHeight = fontSize.y * 2;

        for (size_t i = 0; i < fbResponse->framebuffer_count; i++)
        {
            const auto sourceFb = fbResponse->framebuffers[i];
            ASSERT_(sourceFb->height > reservedHeight);

            const size_t sourceWidth = static_cast<size_t>(sourceFb->width);
            const size_t sourceHeight = static_cast<size_t>(sourceFb->height);
            const GTFramebuffer logFb
            {
                .address = reinterpret_cast<uintptr_t>(sourceFb->address),
                .size = { sourceWidth, sourceHeight - reservedHeight },
                .pitch = static_cast<size_t>(sourceFb->pitch)
            };
            const GTFramebuffer statsFb
            {
                .address = reinterpret_cast<uintptr_t>(sourceFb->address) + (logFb.size.y * logFb.pitch),
                .size = { sourceWidth, reservedHeight },
                .pitch = static_cast<size_t>(sourceFb->pitch)
            };

            //TOOD: we should honour pixel layout and pass it to terminal renderers
            TerminalHead* head = new TerminalHead();
            bool success = head->logRenderer.Init(DefaultTerminalStyle, logFb);
            success = head->statsRenderer.Init(StatsStyle, statsFb);

            if (!success)
            {
                Log("Failed to init graphical terminal head #%zu.", LogLevel::Error, i);
                head->logRenderer.Deinit();
                head->statsRenderer.Deinit();
                delete head;
                continue;
            }

            head->statsRenderer.DisableCursor();
            const char statsInitStr[] = "\e[H\e[7m\e[2K\r\n\e[2K";
            head->statsRenderer.Write(statsInitStr, sizeof(statsInitStr));

            head->next = nullptr;
            termHeads.PushFront(head);
            Log("Added gterminal head #%zu: %ux%u, bpp=%u, base=0x%tx", LogLevel::Info,
                i, (unsigned)sourceFb->width, (unsigned)sourceFb->height, sourceFb->bpp, logFb.address);
        }

        if (!termHeads.Empty())
        {
            termLogOutput.Write = TerminalWriteCallback;
            termLogOutput.BeginPanic = TerminalBeginPanic;
            AddLogOutput(&termLogOutput);
        }
    };
}
