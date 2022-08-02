#include <devices/GenericDevice.h>
#include <devices/interfaces/GenericBlock.h>
#include <filesystem/Gpt.h>
#include <Log.h>
#include <Locks.h>

namespace Kernel::Devices
{
    void GenericDevice::EventPump()
    {}

    void GenericDevice::PostInit()
    {}

    void GenericBlock::PostInit()
    {
        sl::ScopedSpinlock scopeLock(&lock);
        IoBlockBuffer gptBuffer = { 1 };
        size_t opToken = BeginRead(1, gptBuffer);
        while (!EndRead(opToken));

        const Filesystem::GptHeader* gptHeader = gptBuffer.memory.base.As<Filesystem::GptHeader>();
        if (gptHeader->signature != Filesystem::GptSignature)
            return;

        IoBlockBuffer entriesBuffer = { gptHeader->partEntrySize * gptHeader->partEntryCount / PAGE_FRAME_SIZE + 1};
        opToken = BeginRead(gptHeader->partEntryLba, entriesBuffer);
        while (!EndRead(opToken));

        for (size_t i = 0; i < gptHeader->partEntryCount; i++)
        {
            const Filesystem::GptEntry* entry = entriesBuffer.memory.base.As<Filesystem::GptEntry>(i * gptHeader->partEntrySize);
            bool entryValid = false;
            for (size_t j = 0; j < 16; j++)
            {
                if (entry->partTypeGuid[j] == 0)
                    continue;
                entryValid = true;
                break;
            }

            if (!entryValid)
                continue;

            BlockPartition& part = partitions.EmplaceBack();
            part.blockDeviceId = GetId();
            part.partitionIndex = partitions.Size() - 1;
            part.firstLba = entry->startLba;
            part.lastLba = entry->endLba;

            char nameBuff[36]; //name is encoded using UTF16, resulting in null characters everywhere
            for (size_t j = 0; j < 36; j++)
                nameBuff[j] = (char)entry->name[j];
            
            Logf("Block device %u has valid GPT entry: name=%s, start=%u, end=%u", LogSeverity::Verbose,
                GetId(), nameBuff, entry->startLba, entry->endLba);
        }
    }

    sl::Vector<BlockPartition> GenericBlock::GetParts()
    { return partitions; }
}
