#pragma once

#include <interfaces/driver/Input.h>

namespace Npk::Services
{
    using MagicKeyCallback = void (*)(npk_key_id key);

    void HandleMagicKey(npk_key_id key);
    bool AddMagicKey(npk_key_id key, MagicKeyCallback callback);
    bool RemoveMagicKey(npk_key_id key);
}
