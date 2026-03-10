#include <private/Loader.hpp>
#include "ElfSpec.hpp"

namespace Npk
{
    NpkStatus LoadDriverFromPath(sl::StringSpan path, NsObject* root)
    {
        if (path.Empty())
            return NpkStatus::InvalidArg;

        NsObject* obj;
        if (FindObject(&obj, root, path) != NsStatus::Success)
            return NpkStatus::InvalidArg;

        auto status = LoadDriverFromNsObj(*obj);
        UnrefObject(*obj);

        return status;
    }

    static NpkStatus VmRangeGetBase(uintptr_t* base, VmRange& range)
    {
        *base = range.base;

        return NpkStatus::Success;
    }
    
    NpkStatus LoadDriverFromNsObj(NsObject& obj)
    {
        Log("Attempting to load driver from NSO %p", LogLevel::Trace, 
            &obj);

        if (GetObjectType(obj) != NsObjType::File)
            return NpkStatus::InvalidArg;

        auto& kernelSpace = *MySystemDomain().kernelSpace;

        VmSource* src;
        auto result = GetFileObjectVmSource(&src, obj);
        if (result != NpkStatus::Success)
            return result;

        VmRange* range;
        result = SpaceAttach(&range, kernelSpace, 1, 4, src, 0, {});

        uintptr_t rangeBase;
        result = VmRangeGetBase(&rangeBase, *range);
        if (result != NpkStatus::Success)
        {
            SpaceDetach(kernelSpace, range, true);
            return result;
        }

        uint32_t magic;
        sl::MemCopy(&magic, reinterpret_cast<void*>(rangeBase), sizeof(magic));
        Log("Attempted driver load from NSO %p has magic 0x%" PRIx32,
            LogLevel::Trace, &obj, magic);

        result = SpaceDetach(kernelSpace, range, true);
        if (result != NpkStatus::Success)
            NPK_UNEXPECTED_STATUS(result, LogLevel::Error);

        if (sl::MemCompare(&magic, ExpectedMagic, 4) == 0)
            result = Private::LoadElf(kernelSpace, uintptr_t loadBase, obj);
        else if (sl::MemCompare(&magic, "MZ", 2) == 0)
            result = Private::LoadPe(kernelSpace, uintptr_t loadBase, obj);
        else
            result = NpkStatus::Unsupported;

        if (result != NpkStatus::Success)
        {
            NPK_UNEXPECTED_STATUS(result, LogLevel::Trace);
            return result;
        }

        NPK_UNREACHABLE(); //TODO: linkage
    }

    NpkStatus LoadProgramFromPath(VmSpace& space, sl::StringSpan path, NsObject* root)
    {
        NPK_UNREACHABLE(); (void)path;
    }

    NpkStatus LoadProgramFromNsObj(VmSpace& space, NsObject& obj)
    {
        NPK_UNREACHABLE(); (void)obj;
    }
}
