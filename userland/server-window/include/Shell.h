#pragma once

#include <containers/Vector.h>
#include <Rects.h>
#include <Keys.h>

namespace WindowServer
{
    class Shell
    {
    private:
        sl::Vector<KeyEvent> keyChord;

        bool TryParseChord();

    public:
        void HandleKey(KeyEvent key);
        void Redraw(sl::Vector<sl::UIntRect> damageRects);
    };
}
