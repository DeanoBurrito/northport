#pragma once

#include <stddef.h>
#include <stdint.h>
#include <Colour.h>
#include <Vectors.h>

namespace np::Graphics
{
    //generic image always stores it's data as 0xAABBGGRR (rgba32)
    class GenericImage
    {
    private:
        uint32_t* buffer;
        size_t width;
        size_t height;

    public:
        GenericImage();
        GenericImage(size_t w, size_t h) : GenericImage(w, h, Colours::Black) {};
        GenericImage(size_t w, size_t h, Colour fill);
        GenericImage(size_t w, size_t h, uint32_t* takingOwnershipOf);

        ~GenericImage();
        GenericImage(const GenericImage& other);
        GenericImage& operator=(const GenericImage& other);
        GenericImage(GenericImage&& from);
        GenericImage& operator=(GenericImage&& other);

        [[gnu::always_inline]] inline
        const uint32_t* Data() const
        { return buffer; }
        
        [[gnu::always_inline]] inline
        uint32_t* Data()
        { return buffer; }

        [[gnu::always_inline]] inline
        sl::Vector2u Size() const
        { return { width, height }; }
    };
}
