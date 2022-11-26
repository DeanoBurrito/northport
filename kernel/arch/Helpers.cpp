#include <arch/Timers.h>
#include <debug/Log.h>
#include <Maths.h>
#include <UnitConverter.h>

namespace Npk
{
    sl::Opt<size_t> TryCalibrateTimer(const char* timerName, void (*Start)(), void (*Stop)(), size_t (*ReadTicks)())
    {
        ASSERT(ReadTicks != nullptr, "ReadTicks() is required");
        
        constexpr size_t CalibrationRuns = 5;
        constexpr size_t RequiredRuns = 3;
        constexpr size_t CalibrationMillis = 10;
        constexpr size_t MaxCalibrationAttempts = 3;

        long calibTimes[CalibrationRuns];
        long calibMean = 0;
        
        if (Stop != nullptr)
            Stop();
        for (size_t attempt = 0; attempt < MaxCalibrationAttempts; attempt++)
        {
            for (size_t i = 0; i < CalibrationRuns; i++)
            {
                if (Start != nullptr) 
                    Start();
                size_t begin = ReadTicks();
                PolledSleep(CalibrationMillis * 1'000'000);
                size_t end = ReadTicks();
                if (Stop != nullptr)
                    Stop();

                calibTimes[i] = (long)((end - begin) / CalibrationMillis);
                calibMean += calibTimes[i];
            }

            auto maybeCalib = CoalesceTimerRuns(calibTimes, CalibrationRuns, CalibrationRuns - RequiredRuns);
            if (maybeCalib)
            {
                sl::UnitConversion freqs = sl::ConvertUnits(*maybeCalib * 1000);
                Log("Calibrated %s for %lu ticks/ms (%lu.%lu%shz).", LogLevel::Info, timerName,
                    *maybeCalib, freqs.major, freqs.minor, freqs.prefix);
                return *maybeCalib;
            }
        }

        Log("Failed to calibrate %s, bad calibration data.", LogLevel::Warning, timerName);
        return {};
    }

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