#include <services/MagicKeys.h>
#include <core/RunLevels.h>
#include <core/Log.h>
#include <core/WiredHeap.h>
#include <Locks.h>
#include <containers/List.h>

namespace Npk::Services
{
    struct MagicHandler
    {
        sl::ListHook listHook;
        npk_key_id key;
        MagicKeyCallback callback;
    };

    sl::RunLevelLock<RunLevel::Interrupt> handlersLock;
    sl::List<MagicHandler, &MagicHandler::listHook> handlers;

    void HandleMagicKey(npk_key_id key)
    {
        sl::ScopedLock scopeLock(handlersLock);
        for (auto it = handlers.Begin(); it != handlers.End(); ++it)
        {
            if (it->key != key)
                continue;

            return it->callback(key);
        }

        Log("No handler for magic key: 0x%x", LogLevel::Info, key);
    }

    bool AddMagicKey(npk_key_id key, MagicKeyCallback callback)
    {
        VALIDATE_(callback != nullptr, false);

        sl::ScopedLock scopeLock(handlersLock);
        for (auto it = handlers.Begin(); it != handlers.End(); ++it)
        {
            if (it->key == key)
                return false;
        }

        MagicHandler* store = NewWired<MagicHandler>();
        VALIDATE_(store != nullptr, false);
        store->key = key;
        store->callback = callback;
        handlers.PushBack(store);

        return true;
    }

    bool RemoveMagicKey(npk_key_id key)
    {
        sl::ScopedLock scopeLock(handlersLock);
        for (auto it = handlers.Begin(); it != handlers.End(); ++it)
        {
            if (it->key != key)
                continue;

            handlers.Remove(it);
            DeleteWired(&*it);
        }

        return false;
    }
}
