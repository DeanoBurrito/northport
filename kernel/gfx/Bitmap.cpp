#include <gfx/Bitmap.h>
#include <Memory.h>
#include <Utilities.h>

namespace Kernel::Gfx
{
    Bitmap::Bitmap(Vector2u size, PackedColourFormat format, Colour fillColour)
    {
        this->size = size;
        this->format = format;
        buffer = new uint32_t[size.x * size.y];

        sl::memsetT<uint32_t>(buffer, fillColour.GetPacked(format), size.x * size.y);
    }

    Bitmap::Bitmap(const uint32_t* src, PackedColourFormat format, Vector2u size)
    {
        this->size = size;
        this->format = format;
        buffer = new uint32_t[size.x * size.y];

        sl::memcopy(src, buffer, size.x * size.y * sizeof(uint32_t));
    }

    Bitmap::~Bitmap()
    {
        if (buffer != nullptr)
            delete[] buffer;
        buffer = nullptr;
    }

    Bitmap::Bitmap(const Bitmap& other)
    {
        size = other.size;
        buffer = new uint32_t[size.x * size.y];
        format = Colour::packedFormatDefault;

        if (other.format == Colour::packedFormatDefault)
            sl::memcopy(other.buffer, buffer, size.x * size.y * sizeof(uint32_t));
        else
            Colour::Translate(other.buffer, other.format, buffer, format, size.x * size.y);
    }

    Bitmap& Bitmap::operator=(const Bitmap& other)
    {
        if (buffer != nullptr)
            delete[] buffer;
        
        size = other.size;
        buffer = new uint32_t[size.x * size.y];
        format = Colour::packedFormatDefault;

        if (other.format == Colour::packedFormatDefault)
            sl::memcopy(other.buffer, buffer, size.x * size.y * sizeof(uint32_t));
        else
            Colour::Translate(other.buffer, other.format, buffer, format, size.x * size.y);

        return *this;
    }

    Bitmap::Bitmap(Bitmap&& from)
    {
        sl::Swap(buffer, from.buffer);
        sl::Swap(size, from.size);
        sl::Swap(format, from.format);
    }

    Bitmap& Bitmap::operator=(Bitmap&& from)
    {   
        sl::Swap(buffer, from.buffer);
        sl::Swap(size, from.size);
        sl::Swap(format, from.format);

        return *this;
    }

    bool Bitmap::HasData() const
    { return buffer != nullptr; }

    const uint32_t* Bitmap::Data() const
    { return buffer; }

    Vector2u Bitmap::Size() const
    { return size; }

    void Bitmap::Draw(Devices::SimpleFramebuffer* fb, Gfx::Vector2u where, Gfx::Colour tint) const
    {
        //TODO: implement Bitmap::Draw();
    }
}
