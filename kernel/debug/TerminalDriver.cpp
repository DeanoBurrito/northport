#include <debug/TerminalDriver.h>
#include <debug/Terminal.h>
#include <debug/Log.h>
#include <debug/NanoPrintf.h>
#include <boot/LimineTags.h>
#include <containers/List.h>
#include <UnitConverter.h>

//these headers are needed to gather the info required for the stats display
#include <debug/BakedConstants.h>
#include <drivers/api/Api.h>
#include <memory/Vmm.h>
#include <filesystem/FileCache.h>

namespace Npk::Debug
{
    constexpr const char VersionFormatStr[] = "Kernel v%lu.%lu.%lu (api v%u.%u.%u)";
    constexpr const char VmmFormatStr[] = "[VMM] faults=%lu, anon=%lu.%lu%sB/%lu.%lu%sB (%lu), file=%lu.%lu%sB/%lu.%lu%sB (%lu), mmio=%lu.%lu%sB (%lu)";
    constexpr const char FileCacheFormatStr[] = "[FCache] size=%lu%sB (0x%lx * %lu)";
    //TODO: other info to track and display
    //- interrupts per second, and execution time outside of scheduler (int handlers, dpcs)
    //- PMM and file cache usage.
    constexpr const char VersionPreStr[] = "\e[1;1H";
    constexpr const char VmmPreStr[] = "\e[2;1H";
    constexpr const char FileCachePreStr[] = "\e[1;32H";

    constexpr size_t StatsFormatBuffSize = 128;

    struct TerminalHead
    {
        TerminalHead* next;

        Terminal logRenderer;
        Terminal statsRenderer;
    };

    sl::IntrFwdList<TerminalHead> termHeads;

    void UpdateRenderedStats()
    {
        const size_t verLen = npf_snprintf(nullptr, 0, VersionFormatStr,
            versionMajor, versionMinor, versionRev,
            NP_MODULE_API_VER_MAJOR, NP_MODULE_API_VER_MINOR, NP_MODULE_API_VER_REV) + 1;

        char verStr[verLen]; //TODO: no VLAs!
        npf_snprintf(verStr, verLen, VersionFormatStr,
            versionMajor, versionMinor, versionRev,
            NP_MODULE_API_VER_MAJOR, NP_MODULE_API_VER_MINOR, NP_MODULE_API_VER_REV);

        char formatBuff[StatsFormatBuffSize];
        auto vmmStats = VMM::Kernel().GetStats();
        auto anonWss = sl::ConvertUnits(vmmStats.anonWorkingSize, sl::UnitBase::Binary);
        auto anonRss = sl::ConvertUnits(vmmStats.anonResidentSize, sl::UnitBase::Binary);
        auto fileWss = sl::ConvertUnits(vmmStats.fileWorkingSize, sl::UnitBase::Binary);
        auto fileRss = sl::ConvertUnits(vmmStats.fileResidentSize, sl::UnitBase::Binary);
        auto mmioWss = sl::ConvertUnits(vmmStats.mmioWorkingSize, sl::UnitBase::Binary);

        auto fcInfo = Filesystem::GetFileCacheInfo();
        if (fcInfo.unitSize == 0)
            fcInfo.unitSize = fcInfo.modeMultiple = 1; //hack because terminals can start before file cache is initalized.
        auto fcUnitSizeConv = sl::ConvertUnits(fcInfo.unitSize, sl::UnitBase::Binary);

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

            it->statsRenderer.Write(FileCachePreStr, sizeof(FileCachePreStr));
            const size_t fileCacheLen = npf_snprintf(formatBuff, StatsFormatBuffSize, FileCacheFormatStr,
                fcUnitSizeConv.major, fcUnitSizeConv.prefix, fcInfo.unitSize / fcInfo.modeMultiple, fcInfo.modeMultiple);
            it->statsRenderer.Write(formatBuff, fileCacheLen);
        }
    }

    void TerminalWriteCallback(const char* str, size_t len)
    {
        for (auto it = termHeads.Begin(); it != termHeads.End(); it = it->next)
            it->logRenderer.Write(str, len);
        UpdateRenderedStats();
    }

    void InitEarlyTerminals()
    {
        auto fbResponse = Boot::framebufferRequest.response;
        if (fbResponse == nullptr)
            return;

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

            const GTFramebuffer logFb
            {
                .address = reinterpret_cast<uintptr_t>(sourceFb->address),
                .size = { sourceFb->width, sourceFb->height - reservedHeight },
                .pitch = sourceFb->pitch
            };
            const GTFramebuffer statsFb
            {
                .address = reinterpret_cast<uintptr_t>(sourceFb->address) + (logFb.size.y * logFb.pitch),
                .size = { sourceFb->width, reservedHeight },
                .pitch = sourceFb->pitch
            };

            //TOOD: we should honour pixel layout and pass it to terminal renderers
            TerminalHead* head = new TerminalHead();
            bool success = head->logRenderer.Init(DefaultTerminalStyle, logFb);
            success = head->statsRenderer.Init(DefaultTerminalStyle, statsFb);

            if (!success)
            {
                Log("Failed to init graphical terminal head #%lu.", LogLevel::Error, i);
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
            Log("Added gterminal head #%lu: %lux%lu, bpp=%u, base=0x%lx", LogLevel::Info,
                i, sourceFb->width, sourceFb->height, sourceFb->bpp, logFb.address);
        }

        if (!termHeads.Empty())
            AddEarlyLogOutput(TerminalWriteCallback);
    };
}
