#include <Bitmap.h>
#include <Memory.h>

namespace sl
{
    Bitmap::Bitmap(size_t initialEntries)
    {
        Resize(initialEntries);
    }

    Bitmap::~Bitmap()
    {
        if (buffer != nullptr)
            delete[] buffer;
    }

    void Bitmap::Resize(size_t count)
    {
        if (count <= entryCount)
        {
            entryCount = count;
            return;
        }
        
        uint8_t* newBuffer = new uint8_t[count / 8 + 1];
        if (buffer)
        {
            sl::memset(newBuffer, 0, count / 8 + 1);
            sl::memcopy(buffer, newBuffer, entryCount / 8 + 1);
            delete[] buffer;
        }
        buffer = newBuffer;
        entryCount = count;
    }

    void Bitmap::Reset(bool defaultToCleared)
    {
        if (buffer == nullptr)
            return;
        
        sl::memset(buffer, defaultToCleared ? 0 : 0xFF, entryCount / 8 + 1);
    }

    size_t Bitmap::Size() const
    { return entryCount; }

    bool Bitmap::Set(size_t index)
    { return Set(index, true); }

    bool Bitmap::Set(size_t index, bool state)
    {
        if (buffer == nullptr)
            return false;
        if (index >= entryCount)
            return false;

        const size_t byteOffset = index / 8;
        const size_t bitOffset = index % 8;
        const bool changed = (buffer[byteOffset] & (1 << bitOffset)) != state;
        
        buffer[byteOffset] |= 1 << bitOffset;
        return changed;
    }

    bool Bitmap::Get(size_t index)
    {
        if (buffer == nullptr)
            return false;
        if (index >= entryCount)
            return false;
        
        const size_t byteOffset = index / 8;
        const size_t bitOffset = index % 8;
        return buffer[byteOffset] & (1 << bitOffset);
    }

    bool Bitmap::Clear(size_t index)
    {
        if (buffer == nullptr)
            return false;
        if (index >= entryCount)
            return false;

        const size_t byteOffset = index / 8;
        const size_t bitOffset = index % 8;
        const bool changed = (buffer[byteOffset] & (1 << bitOffset)) != false;

        buffer[byteOffset] &= ~(1 << bitOffset);
        return changed;
    }

    size_t Bitmap::FindFirst(bool cleared)
    {
        for (size_t i = 0; i < entryCount; i++)
        {
            if (!Get(i))
                return i;
        }

        return -1;
    }

    size_t Bitmap::FindAndClaimFirst(bool cleared)
    {
        const size_t found = FindFirst(cleared);
        Set(found, true);
        return found;
    }
}
