#include <drivers/DriverHelpers.h>
#include <debug/Log.h>

namespace Npk::Drivers
{
    bool VerifyIoApi(const npk_device_api* api)
    { 
        auto ioApi = reinterpret_cast<const npk_io_device_api*>(api);

        if (ioApi->begin_op == nullptr)
            return false;
        if (ioApi->end_op == nullptr)
            return false;

        return true;
    }

    bool VerifyFramebufferApi(const npk_device_api* api)
    { 
        auto fbApi = reinterpret_cast<const npk_framebuffer_device_api*>(api);
        
        if (fbApi->get_mode == nullptr)
            return false;

        return true;
    }

    bool VerifyGpuApi(const npk_device_api* api)
    { 
        (void)api;
        return false; 
    }

    bool VerifyKeyboardApi(const npk_device_api* api)
    { 
        (void)api;
        return false; 
    }

    bool VerifyFilesystemApi(const npk_device_api* api)
    {
        auto fsApi = reinterpret_cast<const npk_filesystem_device_api*>(api);

        if (fsApi->enter_cache == nullptr)
            return false;
        if (fsApi->exit_cache == nullptr)
            return false;
        if (fsApi->mount == nullptr)
            return false;
        if (fsApi->unmount == nullptr)
            return false;
        if (fsApi->get_root == nullptr)
            return false;
        if (fsApi->create == nullptr)
            return false;
        if (fsApi->remove == nullptr)
            return false;
        if (fsApi->find_child == nullptr)
            return false;
        if (fsApi->get_dir_listing == nullptr)
            return false;

        return true;
    }

    bool VerifyDeviceApi(const npk_device_api* api)
    {
        switch (api->type)
        {
        case npk_device_api_type::Io:
            return VerifyIoApi(api);
        case npk_device_api_type::Framebuffer:
            return VerifyFramebufferApi(api);
        case npk_device_api_type::Gpu:
            return VerifyGpuApi(api);
        case npk_device_api_type::Keyboard:
            return VerifyKeyboardApi(api);
        case npk_device_api_type::Filesystem:
            return VerifyFilesystemApi(api);
        default:
            return false;
        }
        ASSERT_UNREACHABLE();
    }
}
