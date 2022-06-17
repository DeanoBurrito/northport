#include <Configuration.h>
#include <StringCulture.h>
#include <Locks.h>
#include <Log.h>

/*
This include is special (and perfectly valid). This macro is set by the kernel makefile,
and points to the file containing a bunch of #defines. These defines are things that can
only be known when building the kernel, like the build time or build id.
Unfortunately the only thing we can't include is the hash of the built kernel, since we cant
hash the kernel we're building haha. That's still included in config, but comes from an initdisk file.
*/
#include NORTHPORT_CONFIG_HEADER_FILENAME

namespace Kernel
{
    void Configuration::ParseIntoSlot(ConfigSlot* slot, const sl::String& value, bool lock)
    {
        if (slot->locked)
            return;
        slot->locked = lock;

        if (value.Size() == 0)
        {
            slot->value = ""; //this should resolve to the same empty string literal, used everywhere.
            slot->integer = 1;
            slot->type = ConfigSlotType::Bool;
            return;
        }
        
        //we try to parse values in the following order: bool, int and then fallback to string.
        //so we start with a bool, if the optional has a value it means we successfully parsed this as a boolean type.
        sl::Opt<bool> boolValue = {};
        if (sl::memcmp("false", value.C_Str(), sl::min(5ul, value.Size())) == 0 
            || sl::memcmp("no", value.C_Str(), sl::min(2ul, value.Size())) == 0)
        {
            boolValue = false;
        }
        if (sl::memcmp("true", value.C_Str(), sl::min(4ul, value.Size())) == 0 
            || sl::memcmp("yes", value.C_Str(), sl::min(3ul, value.Size())) == 0)
        {
            boolValue = true;
        }
            
        if (boolValue.HasValue())
        {
            slot->type = ConfigSlotType::Bool;
            slot->integer = boolValue.Value() ? 1 : 0;
            slot->value = value;
            return;
        }

        //next we try to parse an integer. We try for signed integers, since we can underflow those, as per spec.
        sl::Opt<uint32_t> intValue = {};
        if (value[0] == '0')
        {
            uint32_t out = 0;
            if (value.Size() == 1)
                intValue = 0;
            else if (value[1] == 'x' || value[1] == 'X')
                sl::StringCulture::current->TryGetUInt32(&out, value, 2, sl::Base::HEX);
            else if (value[1] == 'b' || value[1] == 'B')
                sl::StringCulture::current->TryGetUInt32(&out, value, 2, sl::Base::BINARY);
            else
                sl::StringCulture::current->TryGetUInt32(&out, value, 1, sl::Base::OCTAL);

            intValue = out;
        }
        else if (sl::StringCulture::current->IsDigit(value[0]))
        {
            uint32_t out = 0;
            sl::StringCulture::current->TryGetUInt32(&out, value, 0);
            intValue = out;
        }
        else if (value[0] == '-' && sl::StringCulture::current->IsDigit(value[1]))
        {
            int32_t out = 0;
            sl::StringCulture::current->TryGetInt32(&out, value, 0);
            intValue = (uint32_t)out; //underflow negative values backed into unsigned range.
        }
        
        if (intValue.HasValue())
        {
            slot->type = ConfigSlotType::Uint;
            slot->integer = *intValue;
            slot->value = value;
            return;
        }

        //all else failed, just store the raw input and call it a day.
        slot->type = ConfigSlotType::String;
        slot->value = value;
    }
    
    Configuration globalKernelConfig;
    Configuration* Configuration::Global()
    { return &globalKernelConfig; }

    void Configuration::Init()
    {
        slots = new sl::Vector<ConfigSlot>();
        slots->EnsureCapacity(64);

        /*
            If you're here because your IDE is giving you errors, these values are only known
            when compiling the kernel. I've thought about placeholder #defines somewhere,
            but it's only annoying when working on this file. -DT
        */
        SetSingle("kernel_build_name", NP_KCONFIG_BUILD_NAME);
        SetSingle("kernel_build_id", NP_KCONFIG_BUILD_ID);
        SetSingle("kernel_build_time", NP_KCONFIG_BUILD_TIME);
        SetSingle("kernel_build_hash", NP_KCONFIG_BUILD_HASH);
        SetSingle("kernel_cpu_arch", NP_KCONFIG_CPU_ARCH);
    }

    void Configuration::PrintCurrent()
    {
        Logf("Kernel current configuration slots: %u", LogSeverity::Verbose, slots->Size());

        for (size_t i = 0; i < slots->Size(); i++)
        {
            const ConfigSlot* slot = &slots->At(i);
            Logf("Slot %u: id=%s, locked=%b, int=0x%x (%u), value=%s", LogSeverity::Verbose,
                i, slot->id.C_Str(), slot->locked, slot->integer, slot->integer, slot->value.C_Str());
        }
    }

    bool Configuration::IsLocked(ConfigId id)
    {
        sl::Opt<ConfigSlot> slot = Get(id);
        if (slot)
            return slot->locked;
        return false;
    }

    void Configuration::Lock(ConfigId id)
    {
        sl::Opt<ConfigSlot> slot = Get(id);
        if (slot)
            slot->locked = true;
    }

    void Configuration::Unlock(ConfigId id)
    {
        sl::Opt<ConfigSlot> slot = Get(id);
        if (slot)
            slot->locked = true;
    }

    sl::Opt<ConfigSlot> Configuration::Get(ConfigId id)
    {
        sl::ScopedSpinlock scopeLock(&lock);

        for (size_t i = 0; i < slots->Size(); i++)
        {
            if (slots->At(i).id == id)
                return slots->At(i);
        }

        return {};
    }

    void Configuration::SetSingle(ConfigId id, const sl::String& value)
    {
        sl::ScopedSpinlock scopeLock(&lock);

        ConfigSlot* slot = nullptr;
        for (size_t i = 0; i < slots->Size(); i++)
        {
            if (slots->At(i).id == id)
            {
                slot = &slots->At(i);
                break;
            }
        }

        if (slot == nullptr)
        {
            slots->EmplaceBack(id);
            slot = &slots->Back();
        }
        else if (slot->locked)
            return; //slot is present and locked, do nothing.

        if (value.EndsWith('!'))
        {
            sl::String tempValue = value;
            tempValue.TrimEnd(1);
            ParseIntoSlot(slot, tempValue, true);
        }
        else
            ParseIntoSlot(slot, value, false);
    }

    void Configuration::SetMany(const sl::String& incomingConfig)
    {
        size_t index = 0;
        while (index < incomingConfig.Size())
        {
            //trim leading whitespace
            while (index < incomingConfig.Size() && (incomingConfig[index] == ' ' || incomingConfig[index] == '\n'))
                index++;

            if (index == incomingConfig.Size())
                return; //it was only trailing whitespace
            
            size_t idLength = 0;
            while (index + idLength < incomingConfig.Size() && incomingConfig[index + idLength] != '=' 
                && incomingConfig[index + idLength] != ' ' && incomingConfig[index + idLength] != '\n')
                idLength++;
            
            size_t valueLength = 0;
            if (index + idLength < incomingConfig.Size() 
                && incomingConfig[index + idLength] == '=')
            {
                //there's a value following it, lets get it's length
                while (index + idLength + 1 + valueLength < incomingConfig.Size() 
                    && incomingConfig[index + idLength + 1 + valueLength] != ' ' && incomingConfig[index + idLength + 1 + valueLength] != '\n')
                    valueLength++;
            }

            const sl::String slotId = incomingConfig.SubString(index, idLength);
            const sl::String slotValue = (valueLength == 0) ? "" : incomingConfig.SubString(index + idLength + 1, valueLength);
            SetSingle(slotId, slotValue);
            
            index += idLength;
            if (valueLength > 0)
                index += valueLength + 1;
        }
    }
}
