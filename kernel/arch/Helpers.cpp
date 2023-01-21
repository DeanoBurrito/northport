#include <arch/Timers.h>
#include <debug/Log.h>
#include <Maths.h>
#include <UnitConverter.h>

namespace Npk
{
    sl::Opt<size_t> CoalesceTimerRuns(long* timerRuns, size_t runCount, size_t allowedFails)
    {
        long mean = 0;
        for (size_t i = 0; i < runCount; i++)
            mean += timerRuns[i];
        mean /= runCount;

        const long deviation = sl::StandardDeviation(timerRuns, runCount);
        size_t validRuns = 0;
        size_t finalTime = 0;

        for (size_t i = 0; i < runCount; i++)
        {
            if (timerRuns[i] < mean - deviation || timerRuns[i] > mean + deviation)
                continue;
            
            validRuns++;
            finalTime += (size_t)timerRuns[i];
        }

        if (validRuns < runCount - allowedFails)
            return {};
        return finalTime / validRuns;
    }
}