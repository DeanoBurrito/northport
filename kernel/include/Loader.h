#pragma once

#include <BufferView.h>
#include <String.h>
#include <Optional.h>
#include <scheduling/Thread.h>

namespace Kernel
{
    //creates a new process, with an initial thread that will run the elf. Returns created thread id
    sl::Opt<size_t> LoadElfFromMemory(sl::BufferView file, Scheduling::ThreadFlags threadFlags);
    sl::Opt<size_t> LoadElfFromFile(const sl::String& filename, Scheduling::ThreadFlags threadFlags);
}
