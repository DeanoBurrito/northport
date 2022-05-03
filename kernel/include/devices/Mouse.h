#pragma once

#include <Vectors.h>
#include <containers/CircularQueue.h>
#include <containers/Vector.h>
#include <devices/interfaces/GenericMouse.h>

namespace Kernel::Devices
{
    class Mouse : public Interfaces::GenericMouse
    {
    private:
        char lock;
        bool initialized;
        sl::CircularQueue<sl::Vector2i>* moveEvents;

        sl::Vector2i eventCompactor;
        size_t compactorHits;

        void Deinit() override;
        void Reset() override;
        sl::Opt<Drivers::GenericDriver*> GetDriverInstance() override;
        size_t ButtonCount() override;
        size_t AxisCount() override;

        void FlushCompactor();

    public:
        static Mouse* Global();
        void Init() override;

        void PushMouseEvent(const sl::Vector2i relativeMove);
        size_t EventsPending();
        sl::Vector<sl::Vector2i> GetEvents();
    };
}
