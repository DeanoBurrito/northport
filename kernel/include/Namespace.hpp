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
     * Recommended practice is to only call this function for types the current
     * module has made available to the system.
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

    /* If `obj` is a valid object this function returns it's type.
     */
    NsObjType GetObjectType(NsObject& obj);

    /* If `obj` is a valid object this function will copy the object's name into
     * `buffer`, limited by `buffer.Size()`. If the buffer is zero sized this
     * function just returns the length the objects name without copying 
     * anything.
     * This function otherwise returns the number of characters copied into
     * `buffer`, if it returns `buffer.Size()` there is possibly more to the
     * object name.
     */
    size_t GetObjectName(sl::StringSpan& buffer, NsObject& obj);

    /* TODO:
     */
    NpkStatus GetFileObjectVmSource(VmSource** vsrc, NsObject& obj);

    /* Creates a new namespace object with the values of `name`, `flags` and
     * extra length after the object struct header specified by `extraLength`.
     * The extra length is in addition to the length specified by NsObjType
     * length field (which was previously set by a call to SetObjectTypeInfo).
     *
     * If the `BorrowedName` flag is set this node will not make a copy of
     * `name` internally, and will reference the memory pointed at by `name`.
     * It is the caller's responsibility to ensure this memory is not freed 
     * before the object struct is. If this flag is clear, a copy of the name
     * is kept alongside the node struct, meaning the caller only needs to
     * keep `name` allocated until this function returns.
     *
     * The `Wired` flag indicates whether the node (and optionally the name
     * buffer) should be allocated from wired or paged memory. If this flag is
     * clear paged memory is used.
     *
     * If success is returned, the constructed object is placed into `*ptr`. 
     * Note that this object is not inserted into the namespace tree, it is
     * freestanding. The caller should call `LinkObject()` on the newly created
     * object at some point.
     */
    NpkStatus CreateObject(NsObject** ptr, NsObjType type, NsObjFlags flags, 
        sl::StringSpan name, size_t extraLength);

    /* Wrapper function for `CreateObject()`. This function passes through all
     * arguments except `name` and `id`, the `id` is appended (in text form)
     * to `name` with an optional separator (the value of `ObjidSeparator`).
     * The flag `BorrowedName` is cleared by this function since it allocates
     * a buffer for the formatted name. The new name buffer is the same type of
     * memory (wired/paged) as the object struct itself, which is determined by
     * the `Wired` flag.
     */
    NpkStatus CreateObjectWithId(NsObject** ptr, NsObjType type, 
        NsObjFlags flags, sl::StringSpan name, size_t id, size_t extraLength);

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
