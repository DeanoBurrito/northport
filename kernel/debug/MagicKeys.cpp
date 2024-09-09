#include <debug/MagicKeys.h>
#include <debug/Log.h>
#include <Locks.h>
#include <tasking/RunLevels.h>
#include <containers/List.h>

namespace Npk::Debug
{
    struct MagicHandler
    {
        MagicHandler* next;

        npk_key_id key;
        MagicKeyCallback callback;
    };

    sl::RunLevelLock<RunLevel::Interrupt> handlersLock;
    sl::IntrFwdList<MagicHandler> handlers;

    void HandleMagicKey(npk_key_id key)
    {
        Log("Handling magic key: 0x%x", LogLevel::Verbose, key);

        sl::ScopedLock scopeLock(handlersLock);
        for (auto it = handlers.Begin(); it != nullptr; it = it->next)
        {
            if (it->key != key)
                continue;

            it->callback(key);
            return;
        }

        Log("No handler for magic key: 0x%x", LogLevel::Info, key);
    }

    bool AddMagicKey(npk_key_id key, MagicKeyCallback callback)
    {
        sl::ScopedLock scopeLock(handlersLock);
        for (auto it = handlers.Begin(); it != nullptr; it = it->next)
        {
            if (it->key == key)
                return false;
        }

        MagicHandler* store = new MagicHandler();
        store->key = key;
        store->callback = callback;
        handlers.PushFront(store);

        return true;
    }

    bool RemoveMagicKey(npk_key_id key)
    {
        sl::ScopedLock scopeLock(handlersLock);
        for (auto it = handlers.Begin(); it != nullptr; it = it->next)
        {
            if (it->key != key)
                continue;

            handlers.Remove(it);
            delete it;
            return true;
        }

        return false;
    }
}
