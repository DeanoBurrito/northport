KERNEL_CXX_FLAGS += -I../libs/np-syslib/include -I../libs/np-driverlib/include

CXX_SRCS = arch/Helpers.cpp \
	boot/CommonInit.cpp boot/LimineTags.cpp \
	config/DeviceTree.cpp config/AcpiTables.cpp \
	cpp/Stubs.cpp cpp/New.cpp cpp/UBSan.cpp \
	debug/Log.cpp debug/Panic.cpp debug/TerminalImage.cpp debug/Terminal.cpp \
	debug/TerminalDriver.cpp debug/Symbols.cpp debug/BakedConstants.cpp \
	drivers/DriverManager.cpp drivers/DriverHelpers.cpp drivers/ElfLoader.cpp \
	filesystem/Filesystem.cpp filesystem/FileCache.cpp filesystem/TreeCache.cpp \
	filesystem/TempFs.cpp filesystem/InitDisk.cpp \
	interfaces/driver/Devices.cpp interfaces/driver/Drivers.cpp \
	interfaces/driver/Filesystem.cpp interfaces/driver/General.cpp interfaces/driver/Io.cpp \
	interfaces/driver/Memory.cpp interfaces/driver/Scheduling.cpp interfaces/driver/Interrupts.cpp \
	interrupts/Router.cpp interrupts/Ipi.cpp \
	io/IoManager.cpp \
	memory/Pmm.cpp memory/Vmm.cpp memory/VmObject.cpp memory/Heap.cpp memory/Slab.cpp \
	memory/Pool.cpp memory/virtual/VmDriver.cpp memory/virtual/AnonVmDriver.cpp \
	memory/virtual/KernelVmDriver.cpp memory/virtual/VfsVmDriver.cpp \
	tasking/Clock.cpp tasking/Threads.cpp tasking/Scheduler.cpp tasking/RunLevels.cpp \
	tasking/Waitable.cpp

AS_SRCS =

ARCH_DIR = arch/$(CPU_ARCH)
include $(ARCH_DIR)/Arch.mk

LD_SCRIPT = $(ARCH_DIR)/Linker.lds
CXX_OBJS = $(patsubst %.cpp, $(BUILD_DIR)/%.cpp.o, $(CXX_SRCS))
AS_OBJS = $(patsubst %.S, $(BUILD_DIR)/%.S.o, $(AS_SRCS))
OBJS = $(AS_OBJS) $(CXX_OBJS)

include $(PROJ_ROOT_DIR)/misc/Formatting.mk

.PHONY: all
all: $(KERNEL_FULL_FILEPATH)

$(KERNEL_FULL_FILEPATH): $(OBJS) $(LD_SCRIPT)
	@printf "$(C_BLUE)[Kernel]$(C_RST) Linking ...\r\n"
	$(LOUD)$(X_LD_BIN) $(OBJS) $(KERNEL_LD_FLAGS) -T $(LD_SCRIPT) -o $(KERNEL_FULL_FILEPATH)
	@printf "$(C_BLUE)[Kernel]$(C_RST) $(C_GREEN)Done.$(C_RST)\r\n"

.PHONY: clean
clean:
	@printf "$(C_BLUE)[Kernel]$(C_RST) Cleaning build files ...\r\n"
	$(LOUD)-rm -r $(BUILD_DIR)
	@printf "$(C_BLUE)[Kernel]$(C_RST) $(C_GREEN)Done.$(C_RST)\r\n"

.PHONY: debug/BakedConstants.cpp
debug/BakedConstants.cpp:
	@printf "$(C_BLUE)[Kernel]$(C_RST) Creating source file for build-time kernel constants\r\n"
	@mkdir -p $(@D)
	@printf "#include <debug/BakedConstants.h>\n \
		namespace Npk::Debug \
		{ \
			const char* targetArchStr = \"$(CPU_ARCH)\"; \
			const char* gitCommitHash = \"$(shell git rev-parse HEAD)\"; \
			const char* gitCommitShortHash = \"$(shell git rev-parse --short HEAD)\"; \
			size_t versionMajor = 0; \
			size_t versionMinor = 2; \
			size_t versionRev = 0; \
		}" > debug/BakedConstants.cpp

$(BUILD_DIR)/%.S.o: %.S
	@printf "$(C_BLUE)[Kernel]$(C_RST) Assembling: $<\r\n"
	@mkdir -p $(@D)
	$(LOUD)$(X_AS_BIN) $(KERNEL_AS_FLAGS) $< -o $@

$(BUILD_DIR)/%.cpp.o: %.cpp
	@printf "$(C_BLUE)[Kernel]$(C_RST) Compiling: $<\r\n"
	@mkdir -p $(@D)
	$(LOUD)$(X_CXX_BIN) $(KERNEL_CXX_FLAGS) -c $< -o $@
