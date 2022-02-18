#pragma once

#include <filesystem/FilesystemDriver.h>
#include <NativePtr.h>

namespace Kernel::Filesystem
{
    Drivers::GenericDriver* CreateNewInitDiskFSDriver();
    
    class InitDiskFSDriver : public FilesystemDriver
    {
    private:
        sl::NativePtr ramdiskBegin;
        sl::NativePtr ramdiskEnd;
        size_t ramdiskSize;

    protected:
        void PopulateNode(VfsNode* root) override;
        bool PrepareForUnmount(bool force) override;

        size_t DoRead(VfsNode* node, size_t fromoffset, uint8_t* toBuffer, size_t toOffset, size_t readLength) override;
        size_t DoWrite(VfsNode* node, size_t toOffset, uint8_t* fromBuffer, size_t fromOffset, size_t writeLength) override;
        void DoFlush(VfsNode* node) override;

    public:
        void Init(Drivers::DriverInitInfo* initInfo) override;
        void Deinit() override;
        void HandleEvent(Drivers::DriverEventType type, void* eventArg) override;
    };
}
