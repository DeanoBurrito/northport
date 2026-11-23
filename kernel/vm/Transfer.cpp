#include <private/Vm.hpp>

namespace Npk::Private
{
    constexpr HeapTag TransferTag = NPK_MAKE_HEAP_TAG("Xfer");
}

namespace Npk
{
    using namespace Private;

    NpkStatus AllocTransferBuffers(TransferBuffer** result, size_t count,
        bool wired)
    {
        if (result == nullptr)
            return NpkStatus::InvalidArg;
        if (count == 0)
            return NpkStatus::InvalidArg;
        if (count > static_cast<size_t>(-1) / sizeof(TransferBuffer))
            return NpkStatus::InvalidArg;

        const size_t allocLen = count * sizeof(TransferBuffer);

        void* ptr = PoolAlloc(allocLen, TransferTag, wired);
        if (ptr == nullptr)
            return NpkStatus::Shortage;

        TransferBuffer* buff = static_cast<TransferBuffer*>(ptr);
        for (size_t i = 0; i < count; i++)
            new(buff + i) TransferBuffer {};

        *result = buff;

        return NpkStatus::Success;
    }

    NpkStatus FreeTransferBuffers(TransferBuffer* buffers, size_t count,
        bool wired)
    {
        if (buffers == nullptr)
            return NpkStatus::InvalidArg;
        if (count == 0)
            return NpkStatus::InvalidArg;
        if (count > static_cast<size_t>(-1) / sizeof(TransferBuffer))
            return NpkStatus::InvalidArg;

        const size_t allocLen = count * sizeof(TransferBuffer);
        PoolFree(buffers, allocLen, TransferTag, wired);

        return NpkStatus::Success;
    }
}
