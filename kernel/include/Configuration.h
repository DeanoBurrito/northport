#pragma once

#include <stdint.h>
#include <String.h>
#include <containers/Vector.h>
#include <Optional.h>

namespace Kernel
{
    enum class ConfigSlotType : uint8_t
    {
        Bool,
        Uint,
        String,
    };

    struct ConfigSlot
    {
        ConfigSlotType type;
        bool locked;
        uint32_t integer;
        sl::String value;
        const sl::String id;

        ConfigSlot(const sl::String& id) : type(ConfigSlotType::Bool), locked(false), integer(0), id(id)
        {}
    };

    using ConfigId = const sl::String&;

    class Configuration
    {
    private:
        sl::Vector<ConfigSlot>* slots; //TODO: this really should be a hashmap
        char lock;

        void ParseIntoSlot(ConfigSlot* slot, const sl::String& value, bool lock);

    public:
        static Configuration* Global();

        void Init();
        void PrintCurrent();

        bool IsLocked(ConfigId id);
        void Lock(ConfigId id);
        void Unlock(ConfigId id);

        sl::Opt<ConfigSlot> Get(ConfigId id);
        void SetSingle(ConfigId id, const sl::String& value);
        void SetMany(const sl::String& incomingConfig);
    };
}
