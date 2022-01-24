#include <devices/SystemClock.h>
#include <Platform.h>

namespace Kernel::Devices
{
    uint64_t uptimeMillis;
    uint64_t bootEpoch; //epoch when uptime started counting
    char timeLock; //sounds pretty futuristic, queue the synthwave

    void SetBootEpoch(uint64_t epoch)
    { 
        ScopedSpinlock scopeLock(&timeLock); //probably unnecessary
        bootEpoch = epoch; 
    }
    
    void IncrementUptime(size_t millis)
    {
        ScopedSpinlock scopeLock(&timeLock);
        uptimeMillis += millis;
    }

    uint64_t GetUptime()
    { return uptimeMillis; }

    bool usingApicForUptime;
    bool UsingApicForUptime()
    { return usingApicForUptime; }

    void SetApicForUptime(bool yes)
    { usingApicForUptime = yes; }
}
