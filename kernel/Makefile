#output name of the kernel binary
TARGET = $(KERNEL_FULL_FILEPATH)
#name of the header file we'll use for our compile-time config
CONFIG_HEADER_TARGET = $(BUILD_DIR)/Configuration.Build.h

CXX_INCLUDE_DIRS = -Iinclude -I$(LIBS_DIR)/np-syscall/include -I$(LIBS_DIR)/np-syslib/include -I$(LIBS_DIR)/np-graphics/include
CXX_FLAGS = $(CXX_GLOBAL_FLAGS) $(CXX_KERNEL_FLAGS) $(CXX_INCLUDE_DIRS) \
	-Wall -Wextra -fno-pie -fstack-protector -fno-omit-frame-pointer -ffreestanding \
	-mno-red-zone -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-3dnow -mcmodel=kernel \
	-std=c++17 -fno-rtti -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables

LD_LIBS = -L$(LIBS_OUTPUT_DIR) -lnp-syslib
LD_SCRIPT = arch/$(CPU_ARCH)/linker.lds
LD_FLAGS = $(LD_LIBS) \
	-nostdlib -zmax-page-size=0x1000 -static --no-dynamic-linker -ztext

ASM_FLAGS = $(GLOBAL_ASM_FLAGS)

CXX_SRCS = InitTasks.cpp Log.cpp Logf.cpp LogFont.cpp Panic.cpp Loader.cpp InterruptManager.cpp Configuration.cpp \
	boot/Limine.cpp boot/KernelEntry.cpp \
	memory/PhysicalMemory.cpp memory/VirtualMemory.cpp memory/Paging.cpp memory/KernelHeap.cpp \
	memory/New.cpp memory/IpcManager.cpp memory/KernelPool.cpp memory/KernelSlab.cpp \
	acpi/AcpiTables.cpp \
	drivers/DriverManager.cpp drivers/BuiltInDrivers.cpp drivers/DriverInitTags.cpp \
	devices/LApic.cpp devices/IoApic.cpp devices/ps2/Ps2Driver.cpp devices/ps2/Ps2Keyboard.cpp devices/pci/PciAddress.cpp \
	devices/ps2/Ps2Mouse.cpp devices/DeviceManager.cpp devices/Keyboard.cpp devices/8254Pit.cpp devices/IoBlockBuffer.cpp \
	devices/SystemClock.cpp devices/PciBridge.cpp devices/pci/BochsGraphicsAdaptor.cpp devices/Mouse.cpp \
	devices/BootFramebuffer.cpp devices/GenericDevice.cpp devices/Rtc.cpp devices/pci/VirtioGraphics.cpp \
	devices/pci/VirtioQueue.cpp devices/pci/VirtioCommon.cpp devices/pci/PciCapabilities.cpp devices/pci/NvmeController.cpp \
	scheduling/Thread.cpp scheduling/ThreadGroup.cpp scheduling/Scheduler.cpp scheduling/BuiltinThreads.cpp \
	sanitizers/UBSanitizer.cpp sanitizers/StackProtector.cpp \
	syscalls/Dispatch.cpp syscalls/Devices.cpp syscalls/Memory.cpp syscalls/Filesystem.cpp \
	syscalls/Ipc.cpp syscalls/Utilities.cpp syscalls/ProgramEvents.cpp syscalls/LocalThreadControl.cpp \
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
	@echo "[Kernel] Compiling C++ source: $<"
	@mkdir -p $(shell dirname $@)
	@$(CXX) $(CXX_FLAGS) -c $< -o $@

#An override for this file, since we want it to regen and then built with each compile
$(BUILD_DIR)/Configuration.cpp.o: Configuration.cpp $(CONFIG_HEADER_TARGET)
	@echo "[Kernel] Compiling C++ source: $<"
	@mkdir -p $(shell dirname $@)
	@$(CXX) $(CXX_FLAGS) -I$(BUILD_DIR) -DNORTHPORT_CONFIG_HEADER_FILENAME="\"$(CONFIG_HEADER_TARGET)\"" -c $< -o $@

#this is where we create & populate the compile-time config header for the kernel
#values ending in '!' are not me getting excited, it indicates that we want to lock
#the config slot to prevent further changes.
.PHONY: $(CONFIG_HEADER_TARGET)
$(CONFIG_HEADER_TARGET):
	@echo "[Kernel] Regenerating compile-time config values"
	@echo "#define NP_KCONFIG_BUILD_NAME \"Northport Kernel!\"" > $(CONFIG_HEADER_TARGET)
	@echo "#define NP_KCONFIG_BUILD_ID \"0!\"" >> $(CONFIG_HEADER_TARGET)
	@echo "#define NP_KCONFIG_BUILD_TIME \"$(shell date)!\"" >> $(CONFIG_HEADER_TARGET)
	@echo "#define NP_KCONFIG_BUILD_HASH \"sha256:$(shell echo $(shell date) $(CXX_FLAGS) $(OBJS) | sha256sum)\"" >> $(CONFIG_HEADER_TARGET)
	@echo "#define NP_KCONFIG_CPU_ARCH \"$(CPU_ARCH)!\"" >> $(CONFIG_HEADER_TARGET)
	@echo "\n" >> $(CONFIG_HEADER_TARGET)
