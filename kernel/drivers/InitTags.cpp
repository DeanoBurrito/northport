#include <drivers/InitTags.h>

namespace Npk::Drivers
{
    sl::Opt<InitTag*> FindTag(void* tags, InitTagType type)
    {
        InitTag* testTag = static_cast<InitTag*>(tags);

        while (testTag != nullptr)
        {
            if (testTag->type == type)
                return testTag;
            testTag = testTag->next;
        }

        return {};
    }
    
    void CleanupTags(void* tags)
    {
        InitTag* testTag = static_cast<InitTag*>(tags);

        while (testTag != nullptr)
        {
            InitTag* deleteMe = testTag;
            testTag = testTag->next;
            delete deleteMe;
        }
    }
}
