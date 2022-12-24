#pragma once

#include <stddef.h>
#include <stdint.h>
#include <Locks.h>
#include <NativePtr.h>

namespace Npk::Devices
{
    enum class DeviceType : size_t
    {
        Keyboard = 0,
        Pointer = 1,
        Framebuffer = 2,
    };

    enum class DeviceStatus
    {
        Offline,
        Starting,
        Online,
        Stopping,
        Error,
    };

    class DeviceManager;

    class GenericDevice
    {
    friend DeviceManager;
    private:
        const DeviceType type;
        size_t id;

    protected:
        DeviceStatus status;
        sl::TicketLock lock;
    
    public:
        GenericDevice(DeviceType type) : type(type), status(DeviceStatus::Offline)
        {}

        virtual ~GenericDevice() = default;

        [[gnu::always_inline]]
        inline DeviceType Type()
        { return type; }

        [[gnu::always_inline]]
        inline size_t Id()
        { return id; }

        virtual bool Init() = 0;
        virtual bool Deinit() = 0;
    };

    struct ColourFormat //TODO: this belongs in a generic graphics library (np-graphics).
    {
        uint8_t redOffset;
        uint8_t greenOffset;
        uint8_t blueOffset;
        uint8_t alphaOffset;
        uint8_t redMask;
        uint8_t greenMask;
        uint8_t blueMask;
        uint8_t alphaMask;

        ColourFormat() = default;

        constexpr ColourFormat(uint8_t r_o, uint8_t g_o, uint8_t b_o, uint8_t a_o, uint8_t r_m, uint8_t g_m, uint8_t b_m, uint8_t a_m)
        : redOffset(r_o), greenOffset(g_o), blueOffset(b_o), alphaOffset(a_o), redMask(r_m), greenMask(g_m), blueMask(b_m), alphaMask(a_m)
        {}
    };

    struct FramebufferMode
    {
        size_t width;
        size_t height;
        size_t bpp;
        ColourFormat format;
    };
    
    class GenericFramebuffer : public GenericDevice
    {
    public:
        GenericFramebuffer() : GenericDevice(DeviceType::Framebuffer)
        {}

        virtual ~GenericFramebuffer() = default;

        virtual bool CanModeset() = 0;
        virtual FramebufferMode CurrentMode() = 0;
        virtual bool SetMode(const FramebufferMode& newMode) = 0;
        
        virtual sl::NativePtr LinearAddress() = 0;
        virtual void BeginDraw() = 0;
        virtual void EndDrawAndFlush() = 0;
    };
}
