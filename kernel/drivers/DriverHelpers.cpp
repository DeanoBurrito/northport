#include <drivers/DriverHelpers.h>
#include <debug/Log.h>

namespace Npk::Drivers
{
    bool VerifyIoApi(const npk_device_api* api)
    { 
        auto ioApi = reinterpret_cast<const npk_io_device_api*>(api);

        VALIDATE_(ioApi->begin_op != nullptr, false);
        VALIDATE_(ioApi->end_op != nullptr, false);

        return true;
    }

    bool VerifyFramebufferApi(const npk_device_api* api)
    { 
        auto fbApi = reinterpret_cast<const npk_framebuffer_device_api*>(api);

        VALIDATE_(fbApi->get_mode != nullptr, false);

        return true;
    }

    bool VerifyGpuApi(const npk_device_api* api)
    { 
        auto gpuApi = reinterpret_cast<const npk_gpu_device_api*>(api);

        VALIDATE_(gpuApi->create_framebuffer != nullptr, false);
        VALIDATE_(gpuApi->destroy_framebuffer != nullptr, false);
        VALIDATE_(gpuApi->set_scanout_framebuffer != nullptr, false);
        VALIDATE_(gpuApi->get_scanout_info != nullptr, false);

        return true; 
    }

    bool VerifyFilesystemApi(const npk_device_api* api)
    {
        auto fsApi = reinterpret_cast<const npk_filesystem_device_api*>(api);

        VALIDATE_(fsApi->enter_cache != nullptr, false);
        VALIDATE_(fsApi->exit_cache != nullptr, false);
        VALIDATE_(fsApi->get_root != nullptr, false);
        VALIDATE_(fsApi->mount != nullptr, false);
        VALIDATE_(fsApi->unmount != nullptr, false);
        VALIDATE_(fsApi->create != nullptr, false);
        VALIDATE_(fsApi->remove != nullptr, false);
        VALIDATE_(fsApi->find_child != nullptr, false);
        VALIDATE_(fsApi->get_attribs != nullptr, false);
        VALIDATE_(fsApi->set_attribs != nullptr, false);
        VALIDATE_(fsApi->read_dir != nullptr, false);

        return true;
    }

    bool VerifySysPowerApi(const npk_device_api* api)
    { 
        (void)api;
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
        case npk_device_api_type::Filesystem:
            return VerifyFilesystemApi(api);
        case npk_device_api_type::SysPower:
            return VerifySysPowerApi(api);
        default:
            return false;
        }
        ASSERT_UNREACHABLE();
    }
}
