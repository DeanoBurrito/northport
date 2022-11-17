#include <arch/Timers.h>
#include <debug/Log.h>
#include <Maths.h>
#include <UnitConverter.h>

namespace Npk
{
    sl::Opt<size_t> TryCalibrateTimer(const char* timerName, void (*PolledSleep)(size_t nanos), void (*Start)(), void (*Stop)(), size_t (*ReadTicks)())
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

            calibMean /= CalibrationRuns;
            const long deviation = sl::StandardDeviation(calibTimes, CalibrationRuns);
            size_t validRuns = 0;
            size_t calibrationTime = 0;
            
            for (size_t i = 0; i < CalibrationRuns; i++)
            {
                if (calibTimes[i] < calibMean - deviation || calibTimes[i] > calibMean + deviation)
                {
                    Log("Dropped %s calibration run: %li ticks/ms", LogLevel::Verbose, timerName, calibTimes[i]);
                    continue;
                }

                validRuns++;
                calibrationTime += (size_t)calibTimes[i];
                Log("Keeping %s calibration run: %li ticks/ms", LogLevel::Verbose, timerName, calibTimes[i]);
            }

            if (validRuns < RequiredRuns)
                continue;

            calibrationTime /= validRuns;
            sl::UnitConversion freqs = sl::ConvertUnits(calibrationTime * 1000);
            Log("Calibrated %s for %lu ticks/ms (%lu.%lu%shz).", LogLevel::Info, timerName,
                calibrationTime, freqs.major, freqs.minor, freqs.prefix);
            return calibrationTime;
        }

        Log("Failed to calibrate %s, bad calibration data.", LogLevel::Warning, timerName);
        return {};
    }
}