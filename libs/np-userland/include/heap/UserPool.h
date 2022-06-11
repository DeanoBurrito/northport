#pragma once

#include <BufferView.h>

namespace np::Userland
{
    struct UserPoolNode
    {
        bool isFree;
        UserPoolNode* prev;
        UserPoolNode* next;
        size_t length;

        UserPoolNode(UserPoolNode* prev, UserPoolNode* next, size_t len)
        : isFree(false), prev(prev), next(next), length(len)
        {}

        [[gnu::always_inline]] inline
        void* Data()
        { return this + 1;}
    };
    
    class UserPool
    {
    private:
        sl::BufferView allocRegion;
        size_t usedBytes;
        UserPoolNode* head;
        UserPoolNode* tail;
        char lock;

        void TrySplit(UserPoolNode* node, size_t allocSize);
        void TryMerge(UserPoolNode* node);
        void Expand(size_t allocSize);

    public:
        void Init(sl::NativePtr base);

        void* Alloc(size_t size);
        bool Free(sl::NativePtr where);
    };
}
