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
	events/Dispatch.cpp events/EventQueue.cpp \
	filesystem/Filesystem.cpp filesystem/FileCache.cpp filesystem/TempFs.cpp \
	interrupts/InterruptManager.cpp interrupts/Ipi.cpp \
	memory/Pmm.cpp memory/Vmm.cpp memory/VmObject.cpp memory/Heap.cpp memory/Slab.cpp \
	memory/Pool.cpp memory/virtual/VmDriver.cpp memory/virtual/AnonVmDriver.cpp \
	memory/virtual/KernelVmDriver.cpp \
	tasking/Clock.cpp tasking/Thread.cpp tasking/Process.cpp tasking/Scheduler.cpp \
	tasking/ServiceThreads.cpp tasking/Waitable.cpp

AS_SRCS =

ARCH_DIR = arch/$(CPU_ARCH)
include $(ARCH_DIR)/Arch.mk

LD_SCRIPT = $(ARCH_DIR)/Linker.lds
CXX_OBJS = $(patsubst %.cpp, $(BUILD_DIR)/%.cpp.o, $(CXX_SRCS))
AS_OBJS = $(patsubst %.S, $(BUILD_DIR)/%.S.o, $(AS_SRCS))
OBJS = $(AS_OBJS) $(CXX_OBJS)

SYMS_AWK_CMD = 'BEGIN{print "\#include <debug/Log.h> \r\n \
	namespace Npk::Debug {\r\n \
	KernelSymbol kernelSymbols[] = {\r\n"} \
	$$1 ~ /^[0-9a-fA-F]+/ && NF > 3 { \
	print "{ 0x" $$2 ", " $$3 ", \"" substr($$0, index($$0, $$8)) "\" }, "} \
	END{print "{ -1ul, 0, \"\"} }; }"}'
SYMS_SRC = $(BUILD_DIR)/KernelSyms.cpp
SYMS_OBJ = $(BUILD_DIR)/KernelSyms.cpp.o
SYMS_DUMMY_SRC = $(BUILD_DIR)/DummySyms.cpp
SYMS_DUMMY_OBJ = $(BUILD_DIR)/DummySyms.cpp.o

include $(PROJ_ROOT_DIR)/misc/Formatting.mk

.PHONY: all
all: $(KERNEL_FULL_FILEPATH)

$(KERNEL_FULL_FILEPATH): $(OBJS) $(LD_SCRIPT)
	@printf "$(C_BLUE)[Kernel]$(C_RST) Linking ...\r\n"
	@printf "#include <debug/Log.h> \r\n namespace Npk::Debug {\r\n \
		KernelSymbol kernelSymbols[] = { { -1ul, 0, \"\"} }; }" > $(SYMS_DUMMY_SRC)
	$(LOUD)$(X_CXX_BIN) $(KERNEL_CXX_FLAGS) -c $(SYMS_DUMMY_SRC) -o $(SYMS_DUMMY_OBJ)
	$(LOUD)$(X_LD_BIN) $(OBJS) $(SYMS_DUMMY_OBJ) $(KERNEL_LD_FLAGS) -T $(LD_SCRIPT) -o $(KERNEL_FULL_FILEPATH)
ifeq ($(INCLUDE_KERNEL_SYMBOLS), yes)
	@printf "$(C_BLUE)[kernel]$(C_RST) Generating symbol table ...\r\n"
	$(LOUD)$(X_READELF_BIN) -s --demangle --wide $(KERNEL_FULL_FILEPATH) | \
		awk $(SYMS_AWK_CMD) > $(SYMS_SRC)
	$(LOUD)$(X_CXX_BIN) $(KERNEL_CXX_FLAGS) -c $(SYMS_SRC) -o $(SYMS_OBJ)
	$(LOUD)$(X_LD_BIN) $(OBJS) $(SYMS_OBJ) $(KERNEL_LD_FLAGS) -T $(LD_SCRIPT) -o $(KERNEL_FULL_FILEPATH)
endif
	@printf "$(C_BLUE)[Kernel]$(C_RST) $(C_GREEN)Done.$(C_RST)\r\n"

.PHONY: clean
clean:
	@printf "$(C_BLUE)[Kernel]$(C_RST) Cleaning build files ...\r\n"
	$(LOUD)-rm -r $(BUILD_DIR)
	@printf "$(C_BLUE)[Kernel]$(C_RST) $(C_GREEN)Done.$(C_RST)\r\n"

$(BUILD_DIR)/%.S.o: %.S
	@printf "$(C_BLUE)[Kernel]$(C_RST) Assembling: $<\r\n"
	@mkdir -p $(@D)
	$(LOUD)$(X_AS_BIN) $(KERNEL_AS_FLAGS) $< -o $@

$(BUILD_DIR)/%.cpp.o: %.cpp
	@printf "$(C_BLUE)[Kernel]$(C_RST) Compiling: $<\r\n"
	@mkdir -p $(@D)
	$(LOUD)$(X_CXX_BIN) $(KERNEL_CXX_FLAGS) -c $< -o $@
