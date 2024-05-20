#include <uacpi/kernel_api.h>
#include <interfaces/driver/Memory.h>
#include <interfaces/driver/Api.h>
#include <interfaces/driver/Interrupts.h>
#include <interfaces/driver/Scheduling.h>
#include <NanoPrintf.h>
#include <Log.h>
#include <Memory.h>
#include <Time.h>
#include <ArchHints.h>

extern "C"
{
    void* uacpi_kernel_alloc(uacpi_size size)
    {
        return npk_heap_alloc(size);
    }

    void* uacpi_kernel_calloc(uacpi_size count, uacpi_size size)
    {
        void* ptr = npk_heap_alloc(count * size);
        if (ptr != nullptr)
            sl::memset(ptr, 0, count * size);
        return ptr;
    }

    //thanks for adding sized free infy :)
    void uacpi_kernel_free(void *mem, uacpi_size size_hint)
    {
        npk_heap_free(mem, size_hint);
    }

    void uacpi_kernel_log(enum uacpi_log_level level, const char* format, ...)
    {
        va_list argsList;
        va_start(argsList, format);
        uacpi_kernel_vlog(level, format, argsList);
        va_end(argsList);
    }

    void uacpi_kernel_vlog(enum uacpi_log_level level, const char* format, uacpi_va_list args)
    {
        constexpr size_t UacpiLogLength = 96;

        npk_log_level npkLevel;
        switch (level)
        {
        case UACPI_LOG_DEBUG: npkLevel = npk_log_level::Debug; break;
        case UACPI_LOG_TRACE: npkLevel = npk_log_level::Verbose; break;
        case UACPI_LOG_INFO: npkLevel = npk_log_level::Info; break;
        case UACPI_LOG_WARN: npkLevel = npk_log_level::Warning; break;
        case UACPI_LOG_ERROR: npkLevel = npk_log_level::Error; break;
        }

        char buffer[UacpiLogLength];
        size_t length = npf_vsnprintf(buffer, UacpiLogLength, format, args);
        if (buffer[length - 1] == '\n') //trim trailing newline, kernel logger handles this
            length--;

        npk_log({ .length = length, .data = buffer }, npkLevel);
    }

    uacpi_u64 uacpi_kernel_get_ticks()
    {
        const npk_monotonic_time uptime = npk_get_monotonic_time();
        const size_t tickNs = (size_t)sl::TimeScale::Nanos / uptime.frequency;

        //uacpi expects each tick to represent 100 nanoseconds
        return (uptime.ticks * tickNs) / 100;
    }

    void uacpi_kernel_stall(uacpi_u8 usec)
    {
        const npk_monotonic_time uptime = npk_get_monotonic_time();
        const size_t tickUs = (size_t)sl::TimeScale::Micros / uptime.frequency;

        const size_t targetTicks = uptime.ticks + (usec / tickUs); //TODO: conversion looks a bit sus?
        while (npk_get_monotonic_time().ticks < targetTicks)
            sl::HintSpinloop();
    }

    void uacpi_kernel_sleep(uacpi_u64 msec)
    {
        const npk_duration dur { .scale = npk_time_scale::Millis, .ticks = msec };
        npk_thread_sleep(npk_current_thread(), dur);
    }

    uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request* req)
    {
        switch (req->type)
        {
        case UACPI_FIRMWARE_REQUEST_TYPE_BREAKPOINT:
            Log("Firmware breakpoint.", LogLevel::Verbose);
            break;
        case UACPI_FIRMWARE_REQUEST_TYPE_FATAL:
            Log("Fatal error from firmware: type=0x%x, error=0x%x, arg=0x%lx", LogLevel::Fatal,
                req->fatal.type, req->fatal.code, req->fatal.arg);
            break;
        }

        return UACPI_STATUS_OK;
    }

    uacpi_status uacpi_kernel_install_interrupt_handler(uacpi_u32 irq, uacpi_interrupt_handler callback, uacpi_handle ctx, uacpi_handle *out_irq_handle)
    {
        VALIDATE_(callback != nullptr, UACPI_STATUS_INVALID_ARGUMENT);
        VALIDATE_(out_irq_handle != nullptr, UACPI_STATUS_INVALID_ARGUMENT);

        npk_interrupt_route* route = new npk_interrupt_route();
        route->callback_arg = ctx;
        route->dpc = nullptr;
        route->callback = (bool (*)(void*))callback;
        /* A note about this cast: the uacpi handler returns whether the interrupt was handled (1)
         * or not handled (0). The northport kernel api uses the return value as a way to suppress 
         * queueing the DPC associated with the interrupt route (if one exists). In our case there
         * is no DPC, so this return value is meaningless. The kernel doesn't support interrupt
         * sharing so we dont have to worry about passing on that info from uacpi.
         */

        if (!npk_claim_interrupt_route(route, 0, irq))
        {
            delete route;
            return UACPI_STATUS_INTERNAL_ERROR;
        }

        *out_irq_handle = route;
        return UACPI_STATUS_OK;
    }

    uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler callback, uacpi_handle irq_handle)
    {
        (void)callback;
        VALIDATE_(irq_handle != nullptr, UACPI_STATUS_INVALID_ARGUMENT);

        auto route = static_cast<npk_interrupt_route*>(irq_handle);
        if (npk_remove_interrupt_route(route))
            return UACPI_STATUS_INTERNAL_ERROR;

        delete route;
        return UACPI_STATUS_OK;
    }

    uacpi_status uacpi_kernel_schedule_work(uacpi_work_type type, uacpi_work_handler callback, uacpi_handle ctx)
    {
        ASSERT_UNREACHABLE();
    }

    uacpi_status uacpi_kernel_wait_for_work_completion()
    {
        ASSERT_UNREACHABLE()
    }
}
