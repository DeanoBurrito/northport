#include <filesystem/Vfs.h>
#include <drivers/DriverManifest.h>
#include <filesystem/FilePath.h>
#include <Log.h>
#include <Locks.h>
#include <Memory.h>

namespace Kernel::Filesystem
{
    VfsNode* VFS::FindNode(const VfsNode* current, const FilePath& path, size_t pathTrimStart) const
    {
        size_t segmentBegin = pathTrimStart;

        if (path.IsAbsolute())
            current = rootNode;

        const sl::Vector<sl::String> pathSegments = path.Segments();
        for (size_t i = 0; i < pathSegments.Size(); i++)
        {
            if (current->children.Empty())
                return nullptr;

            bool foundChild = false;
            for (size_t j = 0; j < current->children.Size(); j++)
            {
                if (current->children[j] == nullptr)
                    continue;
                
                if (current->children[j]->name != pathSegments[i])
                    continue;

                foundChild = true;
                current = current->children[j];
                if (i == pathSegments.Size() - 1)
                    return current->parent->children[j];
                else
                    break;
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

        AddNode(rootNode, "initdisk", VfsNodeType::Directory);

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

    sl::Opt<VfsNode*> VFS::FindNode(const sl::String& absolutePath) const
    {
        VfsNode* found = FindNode(rootNode, absolutePath, 0);

        if (found == nullptr)
            return {};
        else
            return found;
    }

    sl::Opt<VfsNode*> VFS::FindNode(const sl::String& relativePath, VfsNode* top) const
    {
        VfsNode* found = FindNode(top, relativePath, 0);

        if (found == nullptr)
            return {};
        else
            return found;
    }

    bool VFS::AddNode(VfsNode* parent, const sl::String& name, VfsNodeType type)
    {
        if (parent == nullptr)
            return false;
        
        size_t firstFreeIndex = (size_t)-1;
        for (size_t i = 0;i < parent->children.Size(); i++)
        {
            if (parent->children.At(i) == nullptr)
                continue;

            if (parent->children.At(i)->name == name)
                return false; //already a child with that name
        }

        VfsNode* newChild = new VfsNode(parent, name, parent->driver, type);

        NodeChangedEventArgs eventArgs(NodeChangedFlags::Added, newChild);
        if (parent->driver != nullptr)
            parent->driver->HandleEvent(Drivers::DriverEventType::FilesystemNodeChanged, &eventArgs);
        if (eventArgs.cancel)
            return false;

        if (firstFreeIndex == (size_t)-1)
            parent->children.PushBack(newChild);
        else
            parent->children.At(firstFreeIndex) = newChild;
        return true;
    }

    bool VFS::RemoveNode(VfsNode* node)
    {
        if (node == nullptr || node->parent == nullptr)
            return false;
        
        VfsNode* parent = node->parent;
        for (size_t i = 0; i < parent->children.Size(); i++)
        {
            if (parent->children[i] == nullptr)
                continue;

            //TODO: notify node of its coming desctruction, so it can flush any buffered operations.
            if (parent->children.At(i) == node)
            {
                NodeChangedEventArgs eventArgs(NodeChangedFlags::Removed, node);
                if (node->driver != nullptr)
                    node->driver->HandleEvent(Drivers::DriverEventType::FilesystemNodeChanged, &eventArgs);
                if (eventArgs.cancel)
                    return false;
                
                parent->children.At(i) = nullptr;
                delete node;
                return true;
            }
        }

        //weird that we got here, node wasnt in its parent's children?
        return false;
    }
}
