#include <Core.hpp>
#include <Vm.hpp>

namespace Npk
{
    static bool MapBatchOwesIssue(const MapBatch& batch)
    {
        if (batch.count != 0)
            return true;
        if (batch.coarse)
            return true;
        if (!batch.freed.Empty())
            return true;

        return false;
    }

    static void LogMapBatchPrematureDtor(const MapBatch& batch)
    {
        Log("Map batch destroyed prematurely: map=%p ranges=%zu coarse=%s."
            " Cached translations may/will exist, and pages are leaked!",
            LogLevel::Error, (void*)batch.map, batch.count,
            batch.coarse ? "yes" : "no");
    }

    MapBatch::~MapBatch()
    {
        if (MapBatchOwesIssue(*this))
            LogMapBatchPrematureDtor(*this);
    }

    void ResetMapBatch(MapBatch& batch, HwMap* map,
        sl::Span<MapBatchRange> storage)
    {
        NPK_ASSERT(batch.count == 0);
        NPK_ASSERT(batch.freed.Empty());

        batch.map = map;
        batch.storage = storage;
        batch.count = 0;
        batch.coarse = false;
    }

    void FinishMapBatch(MapBatch& batch)
    {
        if (!MapBatchOwesIssue(batch))
        {
            batch.map = nullptr;

            return;
        }

        LogMapBatchPrematureDtor(batch);
    }

    void MapBatchDropRanges(MapBatch& batch)
    {
        batch.count = 0;
        batch.coarse = false;
    }

    void MapBatchAddRange(MapBatch& batch, uintptr_t base, size_t length)
    {
        if (batch.count != 0)
        {
            auto& tail = batch.storage[batch.count - 1];

            if (base == tail.base + tail.length)
            {
                tail.length += length;

                return;
            }

            if (base + length == tail.base)
            {
                tail.base = base;
                tail.length += length;

                return;
            }
        }

        if (batch.count == batch.storage.Size())
        {
            batch.coarse = true;

            return;
        }

        batch.storage[batch.count] = { base, length };
        batch.count++;
    }

    void MapBatchFreePage(MapBatch& batch, PageInfo* page)
    {
        NPK_ASSERT(page != nullptr);

        const auto success = batch.freed.PushBack(page);
        NPK_ASSERT(success);
    }
}
