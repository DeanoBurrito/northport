#include <private/Namespace.hpp>
#include <Maths.hpp>
#include <NanoPrintf.hpp>

namespace Npk
{
    constexpr char ObjIdSeparator[] = "";
    constexpr size_t FixedTypeDescCount = 16;

    struct NsTypeDesc
    {
        sl::ListHook listHook;
        sl::RefCount refcount;

        NsObjType type;
        NsObjDtor destructor;
        size_t baseLength;
        HeapTag heapTag;
    };

    using NsTypeDescList = sl::List<NsTypeDesc, &NsTypeDesc::listHook>;
    using TypeDescRef = sl::Ref<NsTypeDesc, &NsTypeDesc::refcount, nullptr>;

    struct NsDirectory : public NsObject
    {
        NsObjList children;
    };

    static NsObject* rootObj;
    static IplSpinLock<Ipl::Passive> typeDescsLock;
    static NsTypeDesc fixedTypeDescs[FixedTypeDescCount];
    static NsTypeDescList vendorTypeDescs;

    static bool IsVendorType(NsObjType type)
    {
        return static_cast<size_t>(type) 
            >= static_cast<size_t>(NsObjType::VendorBase);
    }

    static TypeDescRef AcquireTypeDesc(NsObjType type)
    {
        TypeDescRef ref {};

        typeDescsLock.Lock();
        if (IsVendorType(type))
        {
            for (auto it = vendorTypeDescs.Begin(); 
                it != vendorTypeDescs.End(); ++it)
            {
                if (it->type != type)
                    continue;

                ref = &*it;
                break;
            }
        }
        else
        {
            const size_t index = static_cast<size_t>(type);
            if (index < FixedTypeDescCount 
                && fixedTypeDescs[index].type == type)
                ref = &fixedTypeDescs[index];
        }
        typeDescsLock.Unlock();

        return ref;
    }

    static void DirectoryObjDtor(void* obj)
    {
        (void)obj;
    }

    static void FileObjDtor(void* obj)
    {
        (void)obj;
    }

    void Private::InitNamespace()
    {
        SetObjectTypeInfo(NsObjType::Directory, DirectoryObjDtor, 
            sizeof(NsDirectory), NamespaceHeapTag);
        SetObjectTypeInfo(NsObjType::File, FileObjDtor, sizeof(NsObject),
            NamespaceHeapTag); //TODO: this should be a hook for the VFS

        NsObjFlags flags = NsObjFlag::Wired;
        auto result = CreateObject(&rootObj, NsObjType::Directory, flags, 
            "/"_span, 0);
        if (result != NpkStatus::Success)
        {
            auto str = StatusStr(result);
            Panic("Namespace init failed, root CreateObject() returned %u (%s)",
                nullptr, result, str);
        }

        rootObj->refcount++; //ensure root obj never reaches refcount of 0.

        Log("Global namespace initialized.", LogLevel::Verbose);
    }

    NpkStatus SetObjectTypeInfo(NsObjType type, NsObjDtor dtor, 
        size_t length, HeapTag tag)
    {
        if (dtor == nullptr)
            return NpkStatus::InvalidArg;
        if (length < sizeof(NsObject))
            return NpkStatus::InvalidArg;

        NsTypeDesc* desc;
        if (IsVendorType(type))
        {
            using namespace Private;

            void* ptr = PoolAllocWired(sizeof(NsTypeDesc), NamespaceHeapTag);
            if (ptr == nullptr)
                return NpkStatus::Shortage;
            desc = new(ptr) NsTypeDesc {};

            typeDescsLock.Lock();
            vendorTypeDescs.PushBack(desc);
        }
        else
        {
            const size_t index = static_cast<size_t>(type);
            if (index >= FixedTypeDescCount)
                return NpkStatus::InvalidArg;

            typeDescsLock.Lock();
            desc = &fixedTypeDescs[index];
            if (desc->refcount == 0)
                desc->refcount = 1;
        }

        desc->type = type;
        desc->destructor = dtor;
        desc->baseLength = length;
        desc->heapTag = tag;
        typeDescsLock.Unlock();

        Log("Set NsObjType 0x%x info: dtor=%p, baseLen=%zu", LogLevel::Verbose,
            type, dtor, length);

        return NpkStatus::Success;
    }

    NpkStatus RemoveVendorNsObjType(NsObjType type, bool wait, bool force)
    {
        if (!IsVendorType(type))
            return NpkStatus::InvalidArg;

        (void)wait;
        (void)force;
        NPK_UNREACHABLE();
    }

    NsObject& GetRootObject()
    {
        NPK_ASSERT(rootObj != nullptr);
        
        return *rootObj;
    }

    NpkStatus FindObject(NsObject** found, NsObject* root, sl::StringSpan path)
    {
        if (path.Empty())
            return NpkStatus::InvalidArg;

        if (path[0] == PathDelimiter)
        {
            root = rootObj;
            path = path.Subspan(1, -1);
        }
        else if (root == nullptr)
            root = rootObj;

        if (root == nullptr || root->type != NsObjType::Directory)
            return NpkStatus::InvalidArg;

        if (!RefObject(*root))
            return NpkStatus::ObjRefFailed;
        if (!AcquireMutex(&root->mutex, sl::NoTimeout))
        {
            UnrefObject(*root);
            return NpkStatus::LockAcquireFailed;
        }

        NsDirectory* dir = static_cast<NsDirectory*>(root);
        while (true)
        {
            //we enter each loop with `dir` locked and its refcount
            //incremented, path is the currently unresolved path text.
            const auto delim = path.Find(PathDelimiter);
            const auto childName = path.Subspan(0, delim);
            path = path.Subspan(delim, -1);

            if (path.Size() == 1 && path[0] == '.')
                continue;
            if (path.Size() == 2 && path[0] == '.' && path[1] == '.')
            {
                NPK_UNREACHABLE(); //TODO: implement this
            }

            auto& children = dir->children;
            NsObject* next = nullptr;
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

                if (RefObject(*it))
                    next = &*it;
                break;
            }

            //we're done with the parent now, release its mutex and refcount
            ReleaseMutex(&dir->mutex);
            UnrefObject(*dir);

            if (next == nullptr)
                return NpkStatus::NotFound;
            if (path.Empty())
            {
                *found = next;

                return NpkStatus::Success;
            }
            if (next->type != NsObjType::Directory)
            {
                //there's more to the path but the next node isn't a directory
                UnrefObject(*next);

                return NpkStatus::BadObject;
            }
            if (!AcquireMutex(&next->mutex, sl::NoTimeout))
        {
                UnrefObject(*next);

                return NpkStatus::LockAcquireFailed;
        }

            dir = static_cast<NsDirectory*>(next);
        }
        NPK_UNREACHABLE();
    }

    bool RefObject(NsObject& obj)
    {
        return sl::IncrementRefCount<NsObject, &NsObject::refcount>(&obj);
    }

    void UnrefObject(NsObject& obj)
    {
        NsObject* target = &obj;
        (void)obj;

        /*
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
        */
    }

    NsObjType GetObjectType(NsObject& obj)
    {
        if (obj.refcount.Load(sl::Relaxed) == 0)
            return NsObjType::Invalid;

        return obj.type;
    }

    size_t GetObjectName(sl::Span<char>& buffer, NsObject& obj)
    {
        if (!RefObject(obj))
            return 0;
        if (!AcquireMutex(&obj.mutex, sl::NoTimeout))
            return 0;

        size_t copyLen = sl::Min(buffer.Size(), obj.name.Size());
        if (buffer.Size() == 0)
            copyLen = obj.name.Size();
        else
        sl::MemCopy(buffer.Begin(), obj.name.Begin(), copyLen);

        ReleaseMutex(&obj.mutex);
        UnrefObject(obj);

        return copyLen;
    }

    NpkStatus CreateObject(void** ptr, NsObjType type, NsObjFlags flags, 
        sl::StringSpan name, size_t extraLength)
    {
        using namespace Private;

        if (ptr == nullptr)
            return NpkStatus::InvalidArg;
        if (name.Empty())
            return NpkStatus::InvalidArg;

        auto typeDesc = AcquireTypeDesc(type);
        if (!typeDesc.Valid())
            return NpkStatus::InvalidArg;

        const bool isWired = flags.Has(NsObjFlag::Wired);
        const size_t realLen = extraLength + typeDesc->baseLength;
        void* poolPtr = PoolAlloc(realLen, typeDesc->heapTag, isWired);
        if (poolPtr == nullptr)
            return NpkStatus::Shortage;

        auto* obj = new(poolPtr) NsObject {};
        if (!ResetMutex(&obj->mutex, 1))
        {
            PoolFree(poolPtr, realLen, typeDesc->heapTag, isWired);
            
            return NpkStatus::InternalError;
        }

        obj->type = type;
        obj->refcount = 1;
        obj->parent = nullptr;
        obj->flags = flags;
        obj->extraLength = extraLength;

        if (flags.Has(NsObjFlag::BorrowedName))
            obj->name = name;
        else
        {
            void* namePtr = PoolAlloc(name.Size(), NamespaceHeapTag, isWired);
            if (namePtr == nullptr)
            {
                PoolFree(poolPtr, realLen, typeDesc->heapTag, isWired);

                return NpkStatus::Shortage;
            }

            sl::MemCopy(namePtr, name.Begin(), name.Size());
            obj->name = sl::StringSpan((char*)namePtr, name.Size());
        }

        typeDesc.Acquire(); //extra ref that is held by the 'obj' instance
        *ptr = obj;

        return NpkStatus::Success;
    }

    NpkStatus CreateObjectWithId(NsObject** ptr, NsObjType type, 
        NsObjFlags flags, sl::StringSpan name, size_t id, size_t extraLength)
    {
        using namespace Private;

        if (flags.Has(NsObjFlag::BorrowedName))
            flags.Clear(NsObjFlag::BorrowedName);

        const char* Format = "%.*s%s%zu";
        const size_t nameLen = npf_snprintf(nullptr, 0, Format,
            (int)name.Size(), name.Begin(), ObjIdSeparator, id);

        const bool isWired = flags.Has(NsObjFlag::Wired);
        void* namePtr = PoolAlloc(nameLen + 1, NamespaceHeapTag, isWired);
        if (namePtr == nullptr)
            return NpkStatus::Shortage;

        npf_snprintf((char*)namePtr, nameLen + 1, Format, (int)name.Size(), 
            name.Begin(), id);
        sl::StringSpan realName((char*)namePtr, nameLen);

        return CreateObject(ptr, type, flags, realName, extraLength);
    }

    NpkStatus RenameObject(NsObject& obj, sl::StringSpan name)
    { NPK_UNREACHABLE(); }

    NpkStatus LinkObject(NsObject& parent, NsObject& child)
    { NPK_UNREACHABLE(); }

    NpkStatus UnlinkObject(NsObject& parent, NsObject& child)
    { NPK_UNREACHABLE(); }
}
