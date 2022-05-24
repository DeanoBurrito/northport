#name of our target file
TARGET = $(KERNEL_FULL_FILEPATH)

CXX_INCLUDE_DIRS = -Iinclude -I$(LIBS_DIR)/np-syscall/include -I$(LIBS_DIR)/np-syslib/include -I$(LIBS_DIR)/np-graphics/include
CXX_FLAGS = $(CXX_GLOBAL_FLAGS) $(CXX_INCLUDE_DIRS) \
	-Wall -Wextra  -fpie -fno-stack-protector -fno-omit-frame-pointer -ffreestanding \
	-mno-red-zone -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-3dnow \
	-std=c++17 -fno-rtti -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables

LD_LIBS = -L$(LIBS_OUTPUT_DIR) -lnp-syslib
LD_SCRIPT = arch/$(CPU_ARCH)/linker.lds
LD_FLAGS = $(LD_LIBS) \
	-nostdlib -zmax-page-size=0x1000 -static -pie --no-dynamic-linker -ztext

ASM_FLAGS = $(GLOBAL_ASM_FLAGS)

CXX_SRCS = BootStuff.cpp KernelMain.cpp InitTasks.cpp Log.cpp Logf.cpp Panic.cpp Loader.cpp \
	InterruptManager.cpp \
	memory/PhysicalMemory.cpp memory/VirtualMemory.cpp memory/Paging.cpp memory/KernelHeap.cpp \
	memory/New.cpp memory/IpcManager.cpp memory/KernelPool.cpp memory/KernelSlab.cpp \
	acpi/AcpiTables.cpp \
	drivers/DriverManager.cpp drivers/BuiltInDrivers.cpp drivers/DriverInitTags.cpp \
	devices/LApic.cpp devices/IoApic.cpp devices/ps2/Ps2Driver.cpp devices/ps2/Ps2Keyboard.cpp devices/pci/PciAddress.cpp \
	devices/ps2/Ps2Mouse.cpp devices/DeviceManager.cpp devices/Keyboard.cpp devices/8254Pit.cpp \
	devices/SystemClock.cpp devices/PciBridge.cpp devices/pci/BochsGraphicsAdaptor.cpp devices/Mouse.cpp \
	devices/StivaleFramebuffer.cpp devices/GenericDevice.cpp devices/pci/PciEndpoints.cpp devices/pci/VirtioGraphics.cpp \
	devices/pci/VirtioQueue.cpp devices/pci/VirtioCommon.cpp devices/pci/PciCapabilities.cpp \
	scheduling/Thread.cpp scheduling/ThreadGroup.cpp scheduling/Scheduler.cpp scheduling/BuiltinThreads.cpp \
	sanitizers/UBSanitizer.cpp \
	syscalls/Dispatch.cpp syscalls/Devices.cpp syscalls/Memory.cpp syscalls/Filesystem.cpp syscalls/Ipc.cpp \
	syscalls/Utilities.cpp syscalls/ProgramEvents.cpp syscalls/LocalThreadControl.cpp \
	filesystem/Vfs.cpp filesystem/VfsNode.cpp filesystem/FilesystemDriver.cpp filesystem/InitDiskFSDriver.cpp \
	filesystem/FilePath.cpp
	
ASM_SRCS = 

ARCH_DIR = arch/$(CPU_ARCH)
include $(ARCH_DIR)/Local.mk

#auto populated vars
CXX_OBJS = $(patsubst %.cpp, $(BUILD_DIR)/%.cpp.o, $(CXX_SRCS))
ASM_OBJS = $(patsubst %.s, $(BUILD_DIR)/%.s.o, $(ASM_SRCS))
OBJS = $(CXX_OBJS) $(ASM_OBJS)

.PHONY: all clean

all: $(OBJS) $(LD_SCRIPT)
	@echo "[Kernel] Linking $(TARGET)"
	@$(LD) $(OBJS) $(LD_FLAGS) -T $(LD_SCRIPT) -o $(KERNEL_FULL_FILEPATH)
	@echo "Done."

clean:
	@echo "[Kernel] Cleaning build dir."
	@rm -r $(BUILD_DIR)
	@echo "[Kernel] Done."

$(BUILD_DIR)/%.s.o: %.s
	@echo "[Kernel] Assembling source: $<"
	@mkdir -p $(shell dirname $@)
	@$(ASM) $(ASM_FLAGS) $< -o $@

$(BUILD_DIR)/%.cpp.o: %.cpp
	@echo "[Kernel] Compiling CPP source: $<"
	@mkdir -p $(shell dirname $@)
	@$(CXX) $(CXX_FLAGS) -c $< -o $@
