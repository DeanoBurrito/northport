#include <core/Config.h>
#include <core/Log.h>
#include <interfaces/loader/Generic.h>
#include <Memory.h>

namespace Npk::Core
{
    constexpr sl::StringSpan AffirmativeStrs[] = { "true", "yes", "yeah" };
    constexpr size_t AffirmStrsCount = sizeof(AffirmativeStrs) / sizeof(sl::StringSpan);

    char* ownedCmdline;
    sl::StringSpan cmdline;

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
        ownedCmdline = nullptr;
        cmdline = GetCommandLine();

        Log("Config store init: %.*s", LogLevel::Info, (int)cmdline.Size(), cmdline.Begin());
    }

    void LateInitConfigStore()
    {
        ownedCmdline = new char[cmdline.Size()];
        sl::memcopy(cmdline.Begin(), ownedCmdline, cmdline.Size());
        cmdline = sl::StringSpan(ownedCmdline, cmdline.Size());
    }

    sl::StringSpan GetConfig(sl::StringSpan key)
    {
        if (key.Empty())
            return {};
        if (key[key.Size() - 1] == 0)
            key = key.Subspan(0, key.Size() - 1);

        sl::StringSpan source = cmdline;
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
