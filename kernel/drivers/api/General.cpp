#include <drivers/api/Api.h>
#include <debug/Log.h>
#include <arch/Platform.h>
#include <drivers/DriverManager.h>

extern "C"
{
    using namespace Npk;

    [[gnu::used]]
    void npk_log(REQUIRED const char* str, npk_log_level level)
    {
        auto driver = Drivers::DriverManager::Global().GetShadow();
        ASSERT_(driver.Valid());

        Log("(driver:%s) %s", static_cast<LogLevel>(level), driver->friendlyName.C_Str(), str);
    }

    [[gnu::used]]
    void npk_panic(REQUIRED const char* why)
    {
        Panic(why);
        ASSERT_UNREACHABLE();
    }

    //helper function for if a driver tries to perform a legacy PCI access on archs
    //where it doesnt make sense.
    [[noreturn, gnu::unused]]
    static void HandleBadLegacyAccess()
    {
        Log("Legacy (port-based) PCI access not supported on this architecture", LogLevel::Error);
        ASSERT_UNREACHABLE();
        //TODO: we should crash the driver here to protect the kernel, instead of just returning.
    }

    constexpr uint32_t PciEnableConfig = 0x80000000;

    [[gnu::used]]
    uint32_t npk_pci_legacy_read32(uintptr_t addr)
	{
#ifdef __x86_64__
        Out32(PortPciAddr, (uint32_t)addr | PciEnableConfig);
        return In32(PortPciData);
#else
        HandleBadLegacyAccess();
#endif
	}

    [[gnu::used]]
    void npk_pci_legacy_write32(uintptr_t addr, uint32_t data)
	{
#ifdef __x86_64__
        Out32(PortPciAddr, (uint32_t)addr | PciEnableConfig);
        Out32(PortPciData, data);
#else
        HandleBadLegacyAccess();
#endif
	}
}
