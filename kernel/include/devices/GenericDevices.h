#pragma once

#include <stddef.h>
#include <stdint.h>
#include <Locks.h>
#include <NativePtr.h>
#include <Span.h>

namespace Npk::Devices
{
    enum class DeviceType : size_t
    {
        Keyboard = 0,
        Pointer = 1,
        Framebuffer = 2,
        Serial = 3,
        Block = 4,
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

    namespace ColourFormats 
    {
        constexpr ColourFormat R8G8B8A8 = { 0, 8, 16, 24, 0xFF, 0xFF, 0xFF, 0xFF };
    }

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
        virtual void EndDraw() = 0;
    };

    class GenericSerial : public GenericDevice
    {
    public:
        GenericSerial() : GenericDevice(DeviceType::Serial)
        {}

        virtual ~GenericSerial() = default;
        
        virtual void Write(sl::Span<uint8_t> buffer) = 0;
        virtual size_t Read(sl::Span<uint8_t> buffer) = 0;
        virtual bool InputAvailable() = 0;
    };

    class GenericBlock : public GenericDevice
    {
    public:
        GenericBlock() : GenericDevice(DeviceType::Block)
        {}

        virtual ~GenericBlock() = default;

        virtual void* BeginRead(size_t startLba, size_t lbaCount, sl::NativePtr buffer) = 0;
        virtual bool EndRead(void* token) = 0;
        virtual void* BeginWrite(size_t startLba, size_t lbaCount, sl::NativePtr buffer) = 0;
        virtual bool EndWrite(void* token) = 0;
    };
}
