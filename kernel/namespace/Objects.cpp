#include <NamespacePrivate.hpp>

namespace Npk
{
    static NsObject* rootObj;

    void Private::InitNamespace()
    {
        void* ptr = HeapAllocNonPaged(sizeof(NsObject), NamespaceHeapTag);
        NPK_ASSERT(ptr != nullptr);

        rootObj = new(ptr) NsObject{};
        rootObj->heapTag = NamespaceHeapTag;
        rootObj->refcount = 1;
        rootObj->length = sizeof(NsObject);
        ResetWaitable(&rootObj->mutex, WaitableType::Mutex, 1);

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
                //relase the parent's mutex.
                
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
        if (!sl::DecrementRefCount<NsObject, &NsObject::refcount>(&obj))
            return;

        //arbitrarily high value, just to check for any wraparound bugs.
        NPK_ASSERT(obj.refcount.Load(sl::Relaxed) < 0x12345678); //TODO: make this non-fatal

        if (&obj == rootObj)
        {
            Log("Root namespace object reached a refcount of 0!?", 
                LogLevel::Error);
            obj.refcount++;
            return;
        }

        //TODO: unref parent, which may do this all over again. We should do this in a loop

        const bool wired = obj.flags.Has(NsObjFlag::Wired);
        if (obj.dtor != nullptr)
            obj.dtor(&obj);

        if (!obj.flags.Has(NsObjFlag::UseNameDirectly))
        {
            void* ptr = const_cast<char*>(obj.name.Begin());
            const size_t len = obj.name.SizeBytes();

            if (wired)
                HeapFreeNonPaged(ptr, len, obj.heapTag);
            else
                HeapFree(ptr, len, obj.heapTag);
        }
        obj.name = {};

        if (wired)
            HeapFreeNonPaged(&obj, obj.length, obj.heapTag);
        else
            HeapFree(&obj, obj.length, obj.heapTag);
        //TODO: dont bother syncing on the mutex, it canonly be accessed while this object is referenced.
    }

    NsObjRef GetObjectAutoref(NsObject& obj)
    {
        return &obj;
    }

    NsStatus CreateObject(void** ptr, size_t length, NsObjDtor dtor, 
        sl::StringSpan name , HeapTag tag)
    {}

    NsStatus RenameObject(NsObject& obj, sl::StringSpan name)
    {}

    NsStatus LinkObject(NsObject& parent, NsObject& child)
    {}

    NsStatus UnlinkObject(NsObject& parent, NsObject& child)
    {}
}
