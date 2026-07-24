#include <private/Core.hpp>

namespace Npk
{
    NpkStatus ResetCompletion(Completion& comp, CompletionType type, void* data)
    {
    }

    void NotifyCompletion(Completion& comp)
    {
        switch (comp.Type())
        {
        case CompletionType::None:
            return;

        case CompletionType::Condition:
        {
            auto* cond = static_cast<Condition*>(comp.Data());
            SetCondition(cond);

            break;
        }

        case CompletionType::Dpc:
        {
            auto* dpc = static_cast<Dpc*>(comp.Data());
            QueueDpc(dpc);

            break;
        }

        case CompletionType::WorkItem:
        {
            auto* item = static_cast<WorkItem*>(comp.Data());
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
