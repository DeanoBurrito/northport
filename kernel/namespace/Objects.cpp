#include <NamespacePrivate.hpp>

namespace Npk
{
    static NsObject* rootObj;

    void Private::InitNamespace()
    {
        void* ptr = PoolAllocWired(sizeof(NsObject), NamespaceHeapTag);
        NPK_ASSERT(ptr != nullptr);

        rootObj = new(ptr) NsObject{};
        rootObj->heapTag = NamespaceHeapTag;
        rootObj->refcount = 1;
        rootObj->length = sizeof(NsObject);
        ResetMutex(&rootObj->mutex, 1);

        Log("Global namespace initialized.", LogLevel::Verbose);
    }

    NsObject& GetRootObject()
    {
        NPK_ASSERT(rootObj != nullptr);
        
        return *rootObj;
    }

    NsStatus FindObject(NsObject** found, NsObject* root, sl::StringSpan path)
    {
        NPK_CHECK(!path.Empty(), NsStatus::InvalidArg);

        if (path[0] == PathDelimiter)
        {
            root = rootObj;
            path = path.Subspan(1, -1);
        }
        else if (root == nullptr)
            root = rootObj;

        NPK_CHECK(root != nullptr, NsStatus::InternalError);

        if (!RefObject(*root))
            return NsStatus::BadObject;
        if (!AcquireMutex(&root->mutex, sl::NoTimeout))
        {
            UnrefObject(*root);
            return NsStatus::InternalError;
        }

        while (true)
        {
            const auto delim = path.Find(PathDelimiter);
            const auto childName = path.Subspan(0, delim);
            path = path.Subspan(delim, -1);

            if (childName.Empty())
                break;

            NsObjList children {}; //TODO: figure this out
            NsObject* child = nullptr;
            for (auto it = children.Begin(); it != children.End(); ++it)
            {
                //We dont need to acquire add a reference to the child here,
                //since we're holding the mutex of its parent.
                //If the child has already reached a refcount of zero, the
                //deletion procedure needs to unlink the child from its parent
                //before it can free the memory. This requires it to acquire
                //the parent's mutex, which we are currently holding, so it
                //will block on us until we release the mutex.
                //If the deletion of the child has begun, the call to
                //`RefObject()` below will fail, so we can abort here and 
                //release the parent's mutex.
                
                if (it->name != childName)
                    continue;

                child = &*it;
                if (!RefObject(*child))
                    child = nullptr;
                break;
            }

            if (child == nullptr)
            {
                ReleaseMutex(&root->mutex);
                UnrefObject(*root);
                break;
            }

            //release the lock on the parent and acquire it on the child,
            //we've incremented the refcount of both objects so neither will
            //be deleted until we allow it (by unrefing them),
            ReleaseMutex(&root->mutex);
            UnrefObject(*root);
            root = child;
            AcquireMutex(&child->mutex, sl::NoTimeout);
        }
        ReleaseMutex(&root->mutex);

        if (!path.Empty())
        {
            UnrefObject(*root);
            return NsStatus::InvalidArg;
        }

        *found = root;
        return NsStatus::Success;
    }

    bool RefObject(NsObject& obj)
    {
        return sl::IncrementRefCount<NsObject, &NsObject::refcount>(&obj);
    }

    void UnrefObject(NsObject& obj)
    {
        NsObject* target = &obj;
        (void)obj;

        while (true)
        {
            if (!sl::DecrementRefCount<NsObject, &NsObject::refcount>(target))
                break;

            if (target == rootObj)
            {
                Log("Root namespace object reached a refcount of 0?!", 
                    LogLevel::Error);
                target->refcount = 1;
                break;
            }
            //TODO: remove from parent's children list

            NsObject* parent = target->parent;
            NPK_ASSERT(parent != nullptr);

            if (target->dtor != nullptr)
                target->dtor(target);

            const bool wired = target->flags.Has(NsObjFlag::Wired);
            if (!target->flags.Has(NsObjFlag::UseNameDirectly))
            {
                void* ptr = const_cast<char*>(target->name.Begin());
                const size_t len = target->name.SizeBytes();

                PoolFree(ptr, len, target->heapTag, wired);
            }

            target->name = {};
            PoolFree(target, target->length, wired, target->heapTag);

            target = parent;
        }
    }

    NsObjRef GetObjectAutoref(NsObject& obj)
    {
        return &obj;
    }

    NsStatus CreateObject(void** ptr, size_t length, NsObjDtor dtor, 
        sl::StringSpan name , HeapTag tag)
    { NPK_UNREACHABLE(); }

    NsStatus RenameObject(NsObject& obj, sl::StringSpan name)
    { NPK_UNREACHABLE(); }

    NsStatus LinkObject(NsObject& parent, NsObject& child)
    { NPK_UNREACHABLE(); }

    NsStatus UnlinkObject(NsObject& parent, NsObject& child)
    { NPK_UNREACHABLE(); }
}
