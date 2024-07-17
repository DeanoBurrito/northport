#include <config/ConfigStore.h>
#include <boot/LimineTags.h>
#include <debug/Log.h>
#include <String.h>
#include <Memory.h>

namespace Npk::Config
{
    constexpr sl::StringSpan AffirmativeStrs[] = { "true", "yes", "yeah" };
    constexpr size_t AffirmStrsCount = sizeof(AffirmativeStrs) / sizeof(sl::StringSpan);

    sl::String cmdLine;

    static bool IsDigit(const char c, size_t base)
    {
        if (base <= 10)
            return c >= '0' && c <= ('0' + (char)base);

        const char c10 = c - 10;
        const char b10 = base - 10;
        return (c >= '0' && c <= '9') || (c10 >= 'a' && c10 <= ('a' + b10))
            || (c10 >= 'A' && c10 <= ('A' + b10));
        return false;
    }

    static size_t ParseNum(sl::StringSpan input)
    {
        if (input.Empty())
            return 0;
        if (input.Size() == 1 && input[0] == '0')
            return 0;

        size_t base = 10;
        if (input[0] == '0')
        {
            input = input.Subspan(1, -1);
            switch (input[0])
            {
            case 'x':
            case 'X':
                base = 16;
                input = input.Subspan(1, -1);
                break;
            case 'b':
            case 'B':
                base = 2;
                input = input.Subspan(1, -1);
                break;
            default:
                base = 8;
                break;
            }
        }

        size_t value = 0;
        for (size_t i = 0; i < input.Size() && IsDigit(input[i], base); i++)
        {
            char digit = input[i];
            if (digit >= '0' && digit <= '9')
                digit -= 10;
            else if (digit >= 'A' && digit <= 'Z')
                digit -= 'A' - 10;
            else if (digit >= 'a' && digit <= 'z')
                digit -= 'a' - 10;

            value *= base;
            value += digit;
        }

        return value;
    }

    void InitConfigStore()
    {
        cmdLine = sl::String();
        if (Boot::kernelFileRequest.response != nullptr)
        {
            /* Removing const from the string is quite dangerous, I know - however the config
             * store always returns const views of the config string, so we only have to worry
             * about code within this file writing to the formerly const data.
             * We also only use this const-removed version of the cmdLine until memory management
             * is initialized, then we make a separate copy which is writable.
             */
            auto bootCmdLine = const_cast<char*>(Boot::kernelFileRequest.response->kernel_file->cmdline);
            cmdLine = sl::String(bootCmdLine, true);
        }

        Log("Config store init: %.*s", LogLevel::Info, (int)cmdLine.Size(), cmdLine.C_Str());
    }

    void LateInitConfigStore()
    {
        if (Boot::kernelFileRequest.response == nullptr)
            return;

        (void)cmdLine.DetachBuffer();
        auto bootCmdLine = Boot::kernelFileRequest.response->kernel_file->cmdline;
        cmdLine = bootCmdLine; //copy operation
    }

    sl::StringSpan GetConfig(sl::StringSpan key)
    {
        if (key.Empty())
            return {};
        if (key[key.Size() - 1] == 0)
            key = key.Subspan(0, key.Size() - 1);

        sl::StringSpan source = cmdLine.Span();
        while (!source.Empty())
        {
            const sl::StringSpan compare = source.Subspan(0, key.Size());
            if (compare != key || *compare.End() != '=')
            {
                const size_t nextSpace = sl::memfirst(source.Begin(), ' ', source.Size());
                if (nextSpace == source.Size())
                    break;
                source = source.Subspan(nextSpace + 1, -1ul);
                continue;
            }

            //found a match, return the data portion
            const size_t begin = compare.Size() + 1;
            const size_t length = sl::memfirst(source.Begin() + begin, ' ', source.Size() - begin);
            return source.Subspan(begin, length);
        }

        return {};
    }

    size_t GetConfigNumber(sl::StringSpan key, size_t orDefault)
    {
        auto raw = GetConfig(key);
        if (raw.Empty())
            return orDefault;

        if (orDefault == true || orDefault == false) //potential bool value
        {
            for (size_t i = 0; i < AffirmStrsCount; i++)
            {
                auto test = AffirmativeStrs[i].Subspan(0, AffirmativeStrs[i].Size() - 1);
                if (test == raw)
                    return true;
            }
        }

        return ParseNum(raw);
    }
}
