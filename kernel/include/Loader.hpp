#pragma once

#include <Namespace.hpp>

namespace Npk
{
    /* Attempts to load a driver executable into the kernel address space
     * and link it to the kernel API. This function calls `LoadDriverFromNsObj`
     * internally once if it has resolved `path` relative to `root`.
     * If the caller already has a `NsObject` for the driver file, that should
     * be used instead of converting it to a path and calling this function,
     * to avoid TOCTOU issues.
     */
    NpkStatus LoadDriverFromPath(sl::StringSpan path, NsObject* root);

    /* Attempts to load a driver executable into the kernel address space and
     * link it to the kernel API. `obj` should point to a file-type namespace
     * object, containing the driver program data.
     */
    NpkStatus LoadDriverFromNsObj(NsObject& obj);

    /*
     */
    NpkStatus LoadProgramFromPath(VmSpace& space, sl::StringSpan path, NsObject* root);

    /*
     */
    NpkStatus LoadProgramFromNsObj(VmSpace& space, NsObject& obj);
}
