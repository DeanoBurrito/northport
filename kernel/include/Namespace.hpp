#pragma once

#include <Core.hpp>

namespace Npk
{
    /* Defines the path delimiter used by the namespace subsystem.
     */
    constexpr char PathDelimiter = '/';

    enum class NsStatus
    {
        Success,
        InvalidArg,
        BadObject,
        InternalError,
        Shortage,
    };

    enum class NsObjFlag
    {
        UseNameDirectly,
        Wired,
    };

    using NsObjFlags = sl::Flags<NsObjFlag>;

    enum class NsObjType
    {
        Invalid,
        Directory,
        File,
    };

    using NsObjDtor = void(*)(void* obj);

    struct NsObject
    {
        sl::RefCount refcount;
        NsObjDtor dtor;

        Waitable mutex;
        NsObject* parent;
        sl::ListHook siblingHook;
        sl::StringSpan name;
        NsObjFlags flags;
        HeapTag heapTag;
        size_t length;
    };

    using NsObjRef = sl::Ref<NsObject, &NsObject::refcount>;
    using NsObjList = sl::List<NsObject, &NsObject::siblingHook>;

    using Handle = NsObject*;

    constexpr Handle InvalidHandle = nullptr;

    struct HandleTable;

    /* Returns a reference to the top-most object of the global namespace.
     */
    NsObject& GetRootObject();

    /* Attempts to find an object idenfied by `path`, relative to `root`. If
     * `path` begins with the path delimiter ('/'), `root` is set to the global
     * root object.
     * If this function returns success `*found` contains a pointer to the
     * found node specified by `path`, wiht its reference count incremented.
     * It is up to the caller to decrement the reference count when it is 
     * finished with the object.
     */
    NsStatus FindObject(NsObject** found, NsObject* root, sl::StringSpan path);

    /* Increments an object's refcount if it already non-zero. If the refcount
     * is zero, it remains unchanged as it is already being freed. This
     * function returns whether the refcount was successfully incremented or 
     * not. Calls to `RefObject()` should be paired with a matching call to 
     * `UnrefObject()` when the caller is finished with the object.
     */
    bool RefObject(NsObject& obj);

    /* Decrements an object's refcount. If the refcount reaches zero, it becomes
     * 'sticky' and cannot be incremented again. Once in this state the object
     * will be removed from the namespace and should be considered deleted.
     */
    void UnrefObject(NsObject& obj);

    /* Wraps a call to `RefObject()` and returns a RAII type that automatically
     * calls `UnrefObject()` when it is destroyed. This function is really only
     * for kernel internal use, since we can't propagate C++ type details across
     * ABI boundaries (e.g. to modules or userspace).
     */
    NsObjRef GetObjectAutoref(NsObject& obj);

    NsStatus CreateObject(void** ptr, size_t length, NsObjDtor dtor, 
        sl::StringSpan name, HeapTag tag);
    NsStatus RenameObject(NsObject& obj, sl::StringSpan name);
    NsStatus LinkObject(NsObject& parent, NsObject& child);
    NsStatus UnlinkObject(NsObject& parent, NsObject& child);

    /* Attempts to create a new empty handle table. If successful the result is
     * placed in `*table`.
     */
    NsStatus CreateHandleTable(HandleTable** table);

    /* Destroys any remaining active handles in the table, then releases the
     * table resources. Returns whether destruction was successful or not.
     */
    bool DestroyHandleTable(HandleTable& table);

    /* Creates a new handle table and copies any valid handles from `source`.
     * Copied handles increment the refcount of the underlying object and 
     * will be placed in the same slot they occupied in `source`.
     */
    NsStatus DuplicateHandleTable(HandleTable** copy, HandleTable& source);

    NsStatus CreateHandle(Handle* handle, HandleTable& table, NsObject& obj);
    bool DestroyHandle(Handle& handle, HandleTable& table);

    /* If `handle` is valid for the given handle table, this function looks up
     * the namespace object associated with `handle` and places it in 
     * `*object`. If the lookup was successful the object's refcount will be
     * incremented before returning, it is the caller's responsiblity to
     * decrement the refcount again when appropriate.
     * This function will lock the handle table itself, if the mutex
     * is already held by the caller: use `GetHandleValueLocked()` instead.
     * Returns whether `*object` was modified.
     */
    bool GetHandleValue(NsObject** object, Handle& handle, HandleTable& table);

    /* Similar to `GetHandleValue()` except this function assumes the caller
     * is holding the table mutex.
     */
    bool GetHandleValueLocked(NsObject** object, Handle& handle,
        HandleTable& table);
}
