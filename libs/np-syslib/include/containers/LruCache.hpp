#pragma once

#include <Types.hpp>
#include <Span.hpp>
#include <RefCount.hpp>
#include <containers/RBTree.hpp>
#include <containers/List.hpp>

namespace sl
{
    template<typename Key, typename Value, bool (*SetEntry)(size_t slot, Value* value, Key currentKey, Key newKey)>
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
            { return a.key < b.key; }
        };

    private:
        RBTree<Slot, &Slot::treeHook, SlotLessThan> tree;
        List<Slot, &Slot::listHook> freelist;
        Span<Slot> entries;

    public:
        constexpr LruCache() : tree {}, freelist {}, entries {}
        {}

        void Init(Span<Slot> store, Key defaultKey)
        {
            while (!freelist.Empty())
                freelist.PopFront();
            while (tree.GetRoot() != nullptr)
                tree.Remove(tree.GetRoot());
            entries = store;

            for (size_t i = 0; i < entries.Size(); i++)
            {
                entries[i].refs = 0;
                entries[i].cache = this;
                entries[i].key = defaultKey;
                freelist.PushBack(&entries[i]);
            }
        }

        static void Put(Slot* slot)
        {
            slot->cache->freelist.PushBack(slot);
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
                    if (found->refs == 0)
                        freelist.Remove(found);
                    return found;
                }

                if (found->key > key)
                    found = tree.GetLeft(found);
                else
                    found = tree.GetRight(found);
            }

            if (freelist.Empty())
                return {};

            found = freelist.PopFront();
            if (!SetEntry(found - entries.Begin(), &found->value, found->key, key))
            {
                freelist.PushBack(found);
                return nullptr;
            }
            found->key = key;
            tree.Insert(found);
            
            return found;
        }
    };
}
