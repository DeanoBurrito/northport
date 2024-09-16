#include <interfaces/driver/Input.h>
#include <interfaces/Helpers.h>
#include <debug/MagicKeys.h>

extern "C"
{
    DRIVER_API_FUNC
    void npk_send_magic_key(npk_key_id key)
    {
        Npk::Debug::HandleMagicKey(key);
    }
}
