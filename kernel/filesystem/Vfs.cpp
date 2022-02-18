#include <filesystem/Vfs.h>
#include <drivers/DriverManifest.h>
#include <Log.h>
#include <Locks.h>
#include <Memory.h>

namespace Kernel::Filesystem
{
    VfsNode* VFS::FindNode(const VfsNode* current, const sl::String& path, size_t segmentStart)
    {
        const size_t pathLength = path.EndsWith('/') ? path.Size() : path.Size() + 1;

        if (path.At(0) == '/')
        {
            current = rootNode;
            segmentStart++;
        }

        while (segmentStart < pathLength)
        {
            size_t segmentEnd = sl::memfirst(path.C_Str(), segmentStart, '/', path.Size() - segmentStart);
            if (segmentEnd == (size_t)-1)
                segmentEnd = path.Size() - segmentStart;
            segmentEnd++; //end marker is inclusive
            
            if (current->children.Empty())
                return nullptr; //we didnt consume all of the path and ran out of children
            
            bool foundChild = false;
            for (size_t i = 0; i < current->children.Size(); i++)
            {
                if (current->children[i]->name.Size() != segmentEnd - segmentStart)
                    continue; 

                if (sl::memcmp(path.C_Str(), segmentStart, current->children[i]->name.C_Str(), 0, segmentEnd - segmentStart) != 0)
                    continue;

                //names match!
                foundChild = true;
                current = current->children[i];
                segmentStart += current->name.Size() + 1; //+1 for path separator

                if (segmentStart < pathLength)
                    break;
                
                return current->parent->children[i];
            }

            if (!foundChild)
                return nullptr;
        }

        return nullptr;
    }

    VFS globalVfsInstance;
    VFS* VFS::Global()
    { return &globalVfsInstance; }

    void VFS::Init()
    {
        mountPoints = new sl::Vector<VfsMountPoint*>();

        //Mount() protects against mounting to root, so we need to create the root manually.
        rootNode = new VfsNode(nullptr, '/', nullptr, VfsNodeType::Directory);
        VfsMountPoint* rootMount = new VfsMountPoint(nullptr, '/', nullptr);
        mountPoints->PushBack(rootMount);

        //TODO: add createnode/removenode API
        VfsNode* idNode = new VfsNode(rootNode, "initdisk", nullptr, VfsNodeType::Directory);
        rootNode->children.PushBack(idNode);

        Log("Virtual file system initialized.", LogSeverity::Info);
    }

    bool VFS::Mount(FilesystemDriver* driverInstance, const sl::String& prefix)
    {
        if (prefix == '/')
        {
            Log("VFS mount failed: cannot mount at root (nice try though).", LogSeverity::Error);
            return false;
        }
        if (driverInstance == nullptr)
        {
            Log("VFS mount failed: no mounting driver provided.", LogSeverity::Error);
            return false;
        }

        sl::ScopedSpinlock scopeLock(&mountLock);
        size_t freeMountPointIndex = (size_t)-1;
        for (size_t i = 0; i < mountPoints->Size(); i++)
        {
            if (mountPoints->At(i) == nullptr)
            {
                if (freeMountPointIndex == (size_t)-1)
                    freeMountPointIndex = i;
                continue;
            }
            
            if (mountPoints->At(i)->prefix == prefix)
            {
                Logf("VFS mount failed: existing mountpoint at %s", LogSeverity::Error, prefix.C_Str());
                return false;
            }
        }

        //NOTE: for now we're only accepting absolute filepaths
        VfsNode* mountAt = FindNode(rootNode, prefix, 0);
        if (mountAt == nullptr)
        {
            Logf("VFS mount failed: target path does not exist at %s", LogSeverity::Error, prefix.C_Str());
            return false;
        }
        if (mountAt->type != VfsNodeType::Directory)
        {
            Logf("Cannot mount at %s: vfs node is not a directory.", LogSeverity::Error, prefix.C_Str());
            return false;
        }
        if (!mountAt->children.Empty())
        {
            Logf("Cannot mount at %s: directory is not empty.", LogSeverity::Error, prefix.C_Str());
            return false;
        }

        VfsMountPoint* mountPoint = new VfsMountPoint(mountAt, prefix, driverInstance);
        if (freeMountPointIndex != (size_t)-1)
            mountPoints->At(freeMountPointIndex) = mountPoint;
        else
            mountPoints->PushBack(mountPoint);

        driverInstance->PopulateNode(mountAt);
        Logf("Successfully mounted %s at %s", LogSeverity::Info, driverInstance->Manifest()->name, prefix.C_Str());
        return true; 
    }

    void VFS::Unmount(const sl::String& prefix, bool force)
    {
        if (prefix.Size() == 0)
            return;

        sl::ScopedSpinlock scopeLock(&mountLock);
        VfsMountPoint* mountPoint = nullptr;
        
        for (size_t i = 0; i < mountPoints->Size(); i++)
        {
            if (mountPoints->At(i) == nullptr)
                continue;
            
            if (mountPoints->At(i)->prefix == prefix)
            {
                mountPoint = mountPoints->At(i);
                mountPoints->At(i) = nullptr; //reset pointer
                break;
            }
        }

        if (mountPoint == nullptr)
        {
            Logf("Cannot unmount from %s: nothing mounted at this located.", LogSeverity::Error, prefix.C_Str());
            return;
        }
        if (mountPoint->prefix == '/' && mountPoint->mountedAt == nullptr)
        {
            Log("Cannot unmount root directory, nice try though.", LogSeverity::Error);
            return;
        }

        if (mountPoint->driver == nullptr)
            Logf("Mounted directory (%s) has no associated driver. Unmounting may lead to strange consequences.", LogSeverity::Warning, prefix.C_Str());
        
        if (!mountPoint->driver->PrepareForUnmount() && !force)
        {
            Logf("Cannot unmount from %s: driver refused unmount request, try again with force=true.", LogSeverity::Error, prefix.C_Str());
            return;
        }

        //ensure that the folder we mounted in is empty (remove any vfs nodes the driver may have forgotten about)
        for (size_t i = 0; i < mountPoint->mountedAt->children.Size(); i++)
            delete mountPoint->mountedAt->children.At(i);
        
        //leave the node we mounted in, and the driver is part of another subsystem, so we leave it alive.
        const char* driverName = mountPoint->driver == nullptr ? "<no driver name>" : mountPoint->driver->Manifest()->name;
        Logf("Successfully unmounted %s from %s", LogSeverity::Info, driverName, prefix.C_Str());
        delete mountPoint;
    }
}
