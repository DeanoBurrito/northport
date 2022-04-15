#include <formats/GenericImage.h>
#include <Utilities.h>
#include <Memory.h>

namespace np::Graphics
{
    GenericImage::GenericImage()
    {
        width = height = 0;
        buffer = nullptr;
    }

    GenericImage::GenericImage(size_t w, size_t h, Colour fill)
    {
        width = w;
        height = h;
        buffer = new uint32_t[w * h];

        sl::memsetT<uint32_t>(buffer, fill.Pack(RGBA32), w * h);
    }

    GenericImage::GenericImage(size_t w, size_t h, uint32_t* takingOwnershipOf)
    {
        buffer = takingOwnershipOf;
        width = w;
        height = h;
    }

    GenericImage::~GenericImage()
    {
        if (buffer != nullptr)
            delete[] buffer;
        width = height = 0;
    }

    GenericImage::GenericImage(const GenericImage& other)
    {
        width = other.width;
        height = other.height;
        buffer = new uint32_t[width * height];

        sl::memcopy(other.buffer, buffer, width * height * sizeof(uint32_t));
    }

    GenericImage& GenericImage::operator=(const GenericImage& other)
    {
        if (buffer)
            delete[] buffer;
        
        width = other.width;
        height = other.height;
        buffer = new uint32_t[width * height];
        
        sl::memcopy(other.buffer, buffer, width * height * sizeof(uint32_t));
        return *this;
    }

    GenericImage::GenericImage(GenericImage&& from)
    {
        width = height = 0;
        buffer = nullptr;
        
        sl::Swap(width, from.width);
        sl::Swap(height, from.height);
        sl::Swap(buffer, from.buffer);
    }

    GenericImage& GenericImage::operator=(GenericImage&& from)
    {
        if (buffer)
        {
            delete[] buffer;
            buffer = nullptr;
        }
        
        sl::Swap(width, from.width);
        sl::Swap(height, from.height);
        sl::Swap(buffer, from.buffer);

        return *this;
    }
}
