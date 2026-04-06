#pragma once

#include <Core.hpp>

namespace Npk
{
    /* Defines the path delimiter used by the namespace subsystem.
     */
    constexpr char PathDelimiter = '/';

    enum class NsObjFlag
    {
        BorrowedName,
        Wired,
    };

    using NsObjFlags = sl::Flags<NsObjFlag>;

    enum class NsObjType
    {
        Invalid = 0,
        Directory = 1,
        File = 2,
        Session = 3,
        Job = 4,
        Process = 5,
        Thread = 6,

        VendorBase = 0x1000,
    };

    using NsObjDtor = void(*)(void* obj);

    struct NsObject
    {
        sl::RefCount refcount;
        NsObjType type;

        Waitable mutex;
        NsObject* parent;
        sl::ListHook siblingHook;
        sl::StringSpan name;
        NsObjFlags flags;
        uint32_t extraLength;
    };

    using NsObjRef = sl::Ref<NsObject, &NsObject::refcount>;
    using NsObjList = sl::List<NsObject, &NsObject::siblingHook>;

    using Handle = NsObject*;

    constexpr Handle InvalidHandle = nullptr;

    struct HandleTable;

    /* Sets type info for an object type usable by the namespace. This is mostly
     * for internal use but may also be used by vendor-defined types.
     * This function can be called multiple times for a type and will overwrite
     * previously set values, it is the caller's responsibility to avoid that
     * situation if this is undesired behaviour.
     */
    NpkStatus SetObjectTypeInfo(NsObjType type, NsObjDtor dtor, 
        size_t length, HeapTag tag);

    /* Attempts to remove type info for a vendor type, which may be required for
     * unloading a driver module. If `wait` is set, the function only returns
     * successfully purging the type info (this requires all NsObjects using
     * the type to be destroyed). If `wait` is clear but `force` is set this
     * function flags the type info as pending cleanup and unlinks the dtor
     * function (ensuring any reference to external code is broken). This can
     * leak resources but allows the type's external references to be severed
     * while allowing this function to return immediately.
     */
    NpkStatus RemoveVendorObjectType(NsObjType type, bool wait, bool force);

    /* Returns a reference to the top-most object of the global namespace.
     */
    NsObject& GetRootObject();

    /* Attempts to find an object identified by `path`, relative to `root`. If
     * `path` begins with the path delimiter ('/'), `root` is set to the global
     * root object.
     * If this function returns success `*found` contains a pointer to the
     * found node specified by `path`, with its reference count incremented.
     * It is up to the caller to decrement the reference count when it is 
     * finished with the object.
     */
    NpkStatus FindObject(NsObject** found, NsObject* root, sl::StringSpan path);

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

    /* TODO:
     */
    NsObjType GetObjectType(NsObject& obj);

    /* TODO:
     */
    size_t GetObjectName(sl::StringSpan& buffer, NsObject& obj);

    /* TODO:
     */
    size_t GetObjectPath(sl::StringSpan& buffer, NsObject& obj);

    /* TODO:
     */
    NpkStatus GetFileObjectVmSource(VmSource** vsrc, NsObject& obj);

    /* TODO:
     */
    NpkStatus CreateObject(void** ptr, NsObjType type, NsObjFlags flags, 
        sl::StringSpan name, size_t extraLength);

    /* TODO:
     */
    NpkStatus CreateObjectWithId(void** ptr, NsObjType type, NsObjFlags flags, 
        sl::StringSpan name, size_t id, size_t extraLength);

    /* TODO:
     */
    NpkStatus RenameObject(NsObject& obj, sl::StringSpan name);

    /* TODO:
     */
    NpkStatus LinkObject(NsObject& parent, NsObject& child);

    /* TODO:
     */
    NpkStatus UnlinkObject(NsObject& parent, NsObject& child);

    /* Attempts to create a new empty handle table. If successful the result is
     * placed in `*table`.
     */
    NpkStatus CreateHandleTable(HandleTable** table);

    /* Destroys any remaining active handles in the table, then releases the
     * table resources. Returns whether destruction was successful or not.
     */
    bool DestroyHandleTable(HandleTable& table);

    /* Creates a new handle table and copies any valid handles from `source`.
     * Copied handles increment the refcount of the underlying object and 
     * will be placed in the same slot they occupied in `source`.
     */
    NpkStatus DuplicateHandleTable(HandleTable** copy, HandleTable& source);

    /* Finds a free slot in `table` and places a reference to `obj` in it.
     * This reference internally calls `RefObject()` on `obj`, so as long
     * as this handle exists the `obj` will be kept alive.
     * Upon success, a handle referring to `obj` is placed in `*handle`.
     */
    NpkStatus CreateHandle(Handle* handle, HandleTable& table, NsObject& obj);

    /* TODO:
     */
    bool DestroyHandle(Handle& handle, HandleTable& table);

    /* If `handle` is valid for the given handle table, this function looks up
     * the namespace object associated with `handle` and places it in 
     * `*object`. If the lookup was successful the object's refcount will be
     * incremented before returning, it is the caller's responsibility to
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
