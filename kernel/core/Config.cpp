#include <KernelApi.hpp>
#include <Memory.h>

namespace Npk
{
    constexpr char KeyValueDelim = '=';
    constexpr char ElementDelim = ';';

    static sl::StringSpan configStore {};

    void SetConfigStore(sl::StringSpan store)
    {
        configStore = store;

        Log("Config store set to: %.*s", LogLevel::Info, static_cast<int>(store.Size()), store.Begin());
    }

    static bool IsSpace(char c)
    {
        return c == ' ' || c == '\n' || c == '\r' || c == '\t';
    }

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
                digit -= '0';
            else if (digit >= 'A' && digit <= 'Z')
                digit -= 'A' - 10;
            else if (digit >= 'a' && digit <= 'z')
                digit -= 'a' - 10;

            value *= base;
            value += digit;
        }

        return value;
    }

    static sl::Opt<size_t> FindValueByKey(sl::StringSpan key)
    {
        if (key.Empty())
            return {};
        if (key[key.Size() - 1] == 0)
            key = key.Subspan(0, key.Size() - 1);

        for (size_t i = 0; i < configStore.Size(); i++)
        {
            if (configStore[i] != key[0])
                continue;
            if (key != configStore.Subspan(i, key.Size()))
                continue;

            return i + key.Size();
        }
        return {};
    }

    size_t ReadConfigUint(sl::StringSpan key, size_t defaultValue)
    { 
        constexpr sl::StringSpan YesStrs[] =
        {
            "true"_span, "yes"_span, "yeah"_span
        };

        const sl::StringSpan nullValue {};

        sl::StringSpan value = ReadConfigString(key, nullValue);
        if (value == nullValue)
            return defaultValue;

        for (size_t i = 0; i < sizeof(YesStrs) / sizeof(sl::StringSpan); i++)
        {
            if (value == YesStrs[i].Subspan(0, YesStrs[i].Size()))
                return true;
        }

        return ParseNum(value); //defaults to returning zero/false if not a number
    }
    
    sl::StringSpan ReadConfigString(sl::StringSpan key, sl::StringSpan defaultValue)
    { 
        const auto value = FindValueByKey(key);
        if (!value.HasValue())
            return defaultValue;

        size_t offset = *value;
        //consume trailing whitespace of key
        while (offset < configStore.Size() && IsSpace(configStore[offset]))
            offset++;

        if (offset >= configStore.Size() || configStore[offset] != KeyValueDelim)
            return defaultValue; //key is present but has no assigned value
        offset++;

        //consume leading whitespace in value
        while (offset < configStore.Size() && IsSpace(configStore[offset]))
            offset++;

        const size_t valueLength = sl::MemFind(&configStore[offset], 
            ElementDelim, configStore.Size() - offset);

        return configStore.Subspan(offset, valueLength);
    }
}
