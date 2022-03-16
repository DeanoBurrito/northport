#include <filesystem/InitDiskFSDriver.h>
#include <filesystem/VfsNode.h>
#include <filesystem/Vfs.h>
#include <formats/Tar.h>
#include <Memory.h>
#include <Maths.h>
#include <Log.h>

//TODO: bootloader abstraction will replace directly using stivale2 header
#include <boot/Stivale2.h>
namespace Kernel
{
    //there are defined in KernelMain.cpp - cheeky little hack here
    stivale2_tag* FindStivaleTagInternal(uint64_t id);
}

namespace Kernel::Filesystem
{
    Drivers::GenericDriver* CreateNewInitDiskFSDriver()
    { return new InitDiskFSDriver(); }
    
    void InitDiskFSDriver::PopulateNode(VfsNode* root)
    {
        if (ramdiskBegin.ptr == nullptr)
            return;
        
        //parse tar sequentially, adding vfs nodes are entries are found.
        const sl::TarHeader* tar = sl::TarHeader::FromExisting(ramdiskBegin.ptr);
        while (tar != nullptr && sl::NativePtr((size_t)tar).raw < ramdiskEnd.raw)
        {
            if (tar->IsZero())
            {
                if ((tar++)->IsZero())
                    break; //2 sectors zeroed in a row means we've reached the end of the archive.

                //otherwise we just ignore this empty sector
                tar = tar->Next();
                continue;
            }

            if (sl::memcmp(tar->archiveSignature, "ustar", 5) != 0)
                continue;

            Logf("Found initdisk file: %s, size=%lu", LogSeverity::Verbose, tar->Filename().C_Str(), tar->SizeInBytes());
            VfsNodeType fileType = VfsNodeType::File;
            size_t filenameBegin = 0, filenameLength = 0;
            switch (tar->Type())
            {
                case sl::TarEntryType::NormalFile: 
                    fileType = VfsNodeType::File;
                    filenameBegin = tar->Filename().FindLast('/') + 1;
                    if (filenameBegin == 1)
                        filenameBegin = 0;
                    filenameLength = tar->Filename().Size() - filenameBegin;
                    break;
                case sl::TarEntryType::Directory:
                {
                    fileType = VfsNodeType::Directory;
                    string tempFilename = tar->Filename().SubString(0, tar->Filename().Size() - 1);
                    filenameBegin = tempFilename.FindLast('/');
                    filenameLength = tempFilename.Size() - filenameBegin;
                    break;
                }
                default: 
                    Log("Unimplemented tar file type.", LogSeverity::Error); 
                    break;
            }

            VfsNode* parent = root;
            if (filenameBegin > 0)
            {
                auto maybeParent = VFS::Global()->FindNode(tar->Filename().SubString(0, filenameBegin), root);
                if (!maybeParent)
                {
                    Logf("Could not find parent node for initdisk file %s", LogSeverity::Error, tar->Filename().C_Str());
                    parent = nullptr;
                }
                else
                    parent = *maybeParent;
            }

            if (parent)
            {
                VfsNode* fileNode = CreateNode(parent, tar->Filename().SubString(filenameBegin, filenameLength), fileType);
                NodeData(fileNode) = sl::NativePtr((size_t)tar).ptr;
            }

            tar = tar->Next();
        }
    }

    bool InitDiskFSDriver::PrepareForUnmount(bool)
    {
        return false; //just say we're not ready for now, no reason to unload initdisk
    }

    size_t InitDiskFSDriver::DoRead(VfsNode* node, size_t fromOffset, uint8_t* toBuffer, size_t toOffset, size_t readLength)
    {
        if (node == nullptr || NodeData(node) == nullptr || toBuffer == nullptr)
            return 0;

        sl::TarHeader* tar = sl::TarHeader::FromExisting(NodeData(node));
        readLength = sl::min(readLength, tar->SizeInBytes() - fromOffset);

        sl::memcopy(sl::NativePtr(tar).As<void>(sl::TarSectorSize), fromOffset, toBuffer, toOffset, readLength);
        return readLength;
    }

    size_t InitDiskFSDriver::DoWrite(VfsNode*, size_t, uint8_t*, size_t, size_t)
    {
        Log("InitDisk is read only.", LogSeverity::Error);
        return 0;
    }

    void InitDiskFSDriver::DoFlush(VfsNode*)
    {}

    FileDetails InitDiskFSDriver::GetDetails(VfsNode* node)
    {
        sl::TarHeader* tarHeader = sl::NativePtr(NodeData(node)).As<sl::TarHeader>();
        
        return FileDetails(tarHeader->SizeInBytes());
    }

    void InitDiskFSDriver::Init(Drivers::DriverInitInfo*)
    {
        ramdiskBegin = ramdiskEnd = ramdiskSize = 0;
        
        stivale2_struct_tag_modules* modulesTag = reinterpret_cast<stivale2_struct_tag_modules*>(FindStivaleTagInternal(STIVALE2_STRUCT_TAG_MODULES_ID));
        if (modulesTag == nullptr || modulesTag->module_count == 0)
            goto no_initdisk_found;

        for (size_t i = 0; i < modulesTag->module_count; i++)
        {
            if (sl::memcmp(modulesTag->modules[i].string, "northport-initdisk", 18) == 0)
            {
                //module is at least pretending to be our initdisk, use it
                ramdiskBegin.raw = modulesTag->modules[i].begin;
                ramdiskEnd.raw = modulesTag->modules[i].end;
                ramdiskSize = ramdiskEnd.raw - ramdiskBegin.raw;

                Logf("Found \"northport-initdisk\" module, mounting filesystem. begin=0x%lx, bytes=0x%lx", LogSeverity::Info, ramdiskBegin.raw, ramdiskSize);
                break;
            }
        }
        if (ramdiskBegin.ptr == nullptr)
            goto no_initdisk_found;
        
        VFS::Global()->Mount(this, "/initdisk");

        return;
no_initdisk_found:
        Log("Could not find module with name \"northport-initdisk\", some resources may not be available.", LogSeverity::Warning);
        return;
    }

    void InitDiskFSDriver::Deinit()
    {
        Log("InitDisk filesystem driver is being unloaded, this should not happen.", LogSeverity::Error);
    }

    void InitDiskFSDriver::HandleEvent(Drivers::DriverEventType eventType, void* arg)
    {
        if (ramdiskBegin.ptr == nullptr)
            return;
        
        if (eventType == Drivers::DriverEventType::FilesystemNodeChanged)
        {
            NodeChangedEventArgs* eventArgs = static_cast<NodeChangedEventArgs*>(arg);
            eventArgs->cancel = true; //cant add or remove files from initdisk.
        }
    }
}
