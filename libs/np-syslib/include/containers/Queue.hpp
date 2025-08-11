/*
    Parts of this file are based on the work of Drimitry Vyukov (1024cores.net).
    All code used is licensed under BSD-2 clause, and as required license text is below:

    Copyright (c) 2010-2011 Dmitry Vyukov. All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY DMITRY VYUKOV "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
    FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
    OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#pragma once

#include <Atomic.hpp>

namespace sl
{
    template<typename T>
    class QueueMpSc
    {
    public:
        struct Item
        {
            Atomic<Item*> next;
            T data;

            constexpr Item() : next(nullptr), data{}
            {}
        };
    
    private:
        Item stub;
        Atomic<Item*> head;
        Item* tail;

    public:
        constexpr QueueMpSc() : stub{}, head(&stub), tail(&stub)
        {}

        void Push(Item* item)
        {
            item->next.Store(nullptr, Release);
            Item* prev = head.Exchange(item, AcqRel);
            prev->next.Store(item);
        }

        Item* Pop()
        {
            Item* end = tail;
            Item* next = end->next.Load(Acquire);

            if (end == &stub)
            {
                if (next == nullptr)
                    return nullptr;
                
                tail = next;
                end = next;
                next = next->next.Load(Acquire);
            }

            if (next != nullptr)
            {
                tail = next;
                return end;
            }

            Item* begin = head.Load(Acquire);
            if (end != begin)
                return nullptr;

            Push(&stub);
            next = end->next.Load(Acquire);

            if (next != nullptr)
            {
                tail = next;
                return end;
            }
            return nullptr;
        }
    };
}
