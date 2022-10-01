#include <Shell.h>
#include <Logging.h>

namespace WindowServer
{
    bool Shell::TryParseChord()
    {
        np::Userland::Log("Parsed shell chord", LogLevel::Debug);
        return true;
    }
    
    void Shell::HandleKey(KeyEvent key)
    {
        keyChord.PushBack(key);

        if (TryParseChord())
            keyChord.Clear();
    }
}
