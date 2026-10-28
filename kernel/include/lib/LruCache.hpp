#pragma once

#include "Span.hpp"
#include "RefCount.hpp"
#include "RBTree.hpp"
#include "List.hpp"

namespace sl
{
    template<typename Key, typename Value, 
        bool (*SetEntry)(size_t slot, Value* value, Key currentKey, Key newKey)>
    class LruCache
    {
    public:
        struct Slot
        {
            RBTreeHook treeHook;
            ListHook listHook;
            RefCount refs;
            LruCache* cache;
            Key key;
            Value value;
        };

        struct SlotLessThan
        {
            bool operator()(const Slot& a, const Slot& b)
            { 
                return a.key < b.key;
            }
        };

    private:
        RBTree<Slot, &Slot::treeHook, SlotLessThan> tree;
        List<Slot, &Slot::listHook> unused;
        List<Slot, &Slot::listHook> retired;
        Span<Slot> entries;

    public:
        constexpr LruCache() 
            : tree {}, unused {}, retired {}, entries {}
        {}

        void Init(Span<Slot> store, Key defaultKey)
        {
            while (!unused.Empty())
                unused.PopFront();
            while (!retired.Empty())
                retired.PopFront();
            while (tree.GetRoot() != nullptr)
                tree.Remove(tree.GetRoot());
            entries = store;

            for (size_t i = 0; i < entries.Size(); i++)
            {
                entries[i].refs = 0;
                entries[i].cache = this;
                entries[i].key = defaultKey;
                unused.PushBack(&entries[i]);
            }
        }

        static void Put(Slot* slot)
        {
            slot->cache->retired.PushBack(slot);
        }

        using CacheRef = Ref<Slot, &Slot::refs, Put>;
        friend CacheRef;

        CacheRef Get(Key key)
        {
            Slot* found = tree.GetRoot();
            while (found != nullptr)
            {
                if (found->key == key)
                {
                    if (found->refs.Load(sl::Relaxed) == 0)
                        retired.Remove(found);

                    found->refs.Add(1);
                    CacheRef ref = found;
                    found->refs.Sub(1);

                    return ref;
                }

                if (found->key > key)
                    found = tree.GetLeft(found);
                else
                    found = tree.GetRight(found);
            }

            if (!unused.Empty())
                found = unused.PopFront();
            else if (!retired.Empty())
            {
                found = retired.PopFront();
                tree.Remove(found);
            }
            else
                return {};

            if (!SetEntry(found - entries.Begin(), &found->value, found->key, 
                key))
            {
                unused.PushBack(found);
                return {};
            }
            found->key = key;
            tree.Insert(found);
            
            found->refs.Add(1);
            CacheRef ref = found;
            found->refs.Sub(1);

            return ref;
        }
    };
}
