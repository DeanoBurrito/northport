KERNEL_CXX_FLAGS += -I../libs/np-syslib/include

CXX_SRCS = arch/Helpers.cpp \
	boot/CommonInit.cpp boot/LimineTags.cpp boot/LimineBootstrap.cpp \
	config/DeviceTree.cpp config/AcpiTables.cpp \
	cpp/Stubs.cpp cpp/New.cpp cpp/UBSan.cpp \
	debug/Log.cpp debug/NanoPrintf.cpp debug/TerminalImage.cpp debug/Terminal.cpp \
	debug/TerminalDriver.cpp \
	devices/PciAddress.cpp devices/PciBridge.cpp devices/PciCapabilities.cpp \
	devices/DeviceManager.cpp \
	drivers/DriverManager.cpp drivers/BuiltInDrivers.cpp drivers/builtin/BochsVga.cpp \
	drivers/InitTags.cpp drivers/builtin/VirtioTransport.cpp drivers/builtin/VirtioGpu.cpp \
	drivers/builtin/Nvme.cpp \
	interrupts/InterruptManager.cpp interrupts/Ipi.cpp \
	memory/Pmm.cpp memory/Vmm.cpp memory/VmObject.cpp memory/Heap.cpp memory/Slab.cpp \
	memory/Pool.cpp memory/virtual/VmDriver.cpp memory/virtual/AnonVmDriver.cpp \
	memory/virtual/KernelVmDriver.cpp \
	tasking/Clock.cpp tasking/Thread.cpp tasking/Process.cpp tasking/Scheduler.cpp \
	tasking/ServiceThreads.cpp

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
	@echo -e "$(C_BLUE)[Kernel]$(C_RST) Linking ..."
	$(LOUD)$(X_LD_BIN) $(OBJS) $(KERNEL_LD_FLAGS) -T $(LD_SCRIPT) -o $(KERNEL_FULL_FILEPATH)
	@echo -e "$(C_BLUE)[Kernel]$(C_RST) $(C_GREEN)Done.$(C_RST)"

.PHONY: clean
clean:
	@echo -e "$(C_BLUE)[Kernel]$(C_RST) Cleaning build files ..."
	$(LOUD)-rm -r $(BUILD_DIR)
	@echo -e "$(C_BLUE)[Kernel]$(C_RST) $(C_GREEN)Done.$(C_RST)"

$(BUILD_DIR)/%.S.o: %.S
	@echo -e "$(C_BLUE)[Kernel]$(C_RST) Assembling: $<"
	@mkdir -p $(@D)
	$(LOUD)$(X_AS_BIN) $(KERNEL_AS_FLAGS) $< -o $@

$(BUILD_DIR)/%.cpp.o: %.cpp
	@echo -e "$(C_BLUE)[Kernel]$(C_RST) Compiling: $<"
	@mkdir -p $(@D)
	$(LOUD)$(X_CXX_BIN) $(KERNEL_CXX_FLAGS) -c $< -o $@
