#pragma once

#include <stddef.h>
#include <NativePtr.h>

void* malloc(size_t size);
void free(void* ptr);

namespace np::Userland
{
    class GeneralHeap;
    
    struct GeneralHeapNode
    {
    public:
        GeneralHeapNode* prev;
        GeneralHeapNode* next;
        size_t length;
        bool free;

        bool CombineWithNext(GeneralHeap& heap);
        void Split(size_t neededSize, GeneralHeap& heap);
    };
    
    class GeneralHeap
    {
    friend GeneralHeapNode;
    private:
        bool initialized;
        char lock;
        size_t bytesUsed;
        GeneralHeapNode* head;
        GeneralHeapNode* tail;

        void Expand(size_t requiredLength);

    public:
        static GeneralHeap& Default();

        void Init(sl::NativePtr startAddress, size_t length);
        void* Alloc(size_t size);
        void Free(void* ptr);

        template<typename T>
        T* Alloc()
        {
            return reinterpret_cast<T*>(Alloc(sizeof(T)));
        }

        [[gnu::always_inline]] inline
        size_t GetBytesUsed() const
        { return bytesUsed; }
    };
}
