#include <private/Core.hpp>

namespace Npk
{
    NpkStatus ResetCompletion(Completion& comp, CompletionType type, void* data)
    {
        comp.Set(data, type);

        return NpkStatus::Success;
    }

    void NotifyCompletion(CompletionTarget target)
    {
        switch (target.type)
        {
        case CompletionType::None:
            return;

        case CompletionType::Condition:
        {
            auto* cond = static_cast<Condition*>(target.data);
            SetCondition(cond);

            break;
        }

        case CompletionType::Dpc:
        {
            auto* dpc = static_cast<Dpc*>(target.data);
            QueueDpc(dpc);

            break;
        }

        case CompletionType::WorkItem:
        {
            auto* item = static_cast<WorkItem*>(target.data);
            QueueWorkItem(item, {});

            break;
        }

        case CompletionType::Apc:
            //TODO: APC support
            NPK_UNREACHABLE();
            break;

        case CompletionType::EventPort:
            //TODO: event port support
            NPK_UNREACHABLE();
            break;

        default:
            NPK_UNREACHABLE();
        }
    }
}
