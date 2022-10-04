KERNEL_CXX_FLAGS += -I../libs/np-syslib/include

CXX_SRCS = \
	acpi/Tables.cpp \
	boot/CommonInit.cpp boot/LimineTags.cpp \
	cpp/Stubs.cpp cpp/New.cpp cpp/UBSan.cpp \
	debug/Log.cpp debug/LogBackends.cpp debug/NanoPrintf.cpp debug/TerminalImage.cpp \
	debug/Terminal.cpp \
	interrupts/InterruptManager.cpp \
	memory/Pmm.cpp memory/Vmm.cpp memory/Heap.cpp memory/Slab.cpp memory/Pool.cpp

AS_SRCS =

ARCH_DIR = arch/$(CPU_ARCH)
include $(ARCH_DIR)/Arch.mk

LD_SCRIPT = $(ARCH_DIR)/Linker.lds
CXX_OBJS = $(patsubst %.cpp, $(BUILD_DIR)/%.cpp.o, $(CXX_SRCS))
AS_OBJS = $(patsubst %.S, $(BUILD_DIR)/%.S.o, $(AS_SRCS))
OBJS = $(AS_OBJS) $(CXX_OBJS)

.PHONY: all
all: $(KERNEL_FULL_FILEPATH)

$(KERNEL_FULL_FILEPATH): $(OBJS) $(LD_SCRIPT)
	@echo "[Kernel] Linking ..."
	$(LOUD)$(X_LD_BIN) $(OBJS) $(KERNEL_LD_FLAGS) -T $(LD_SCRIPT) -o $(KERNEL_FULL_FILEPATH)
	@echo "[Kernel] Done linking."

.PHONY: clean
clean:
	@echo "[Kernel] Cleaning build files ..."
	$(LOUD)-rm -r $(BUILD_DIR)
	@echo "[Kernel] Done."

$(BUILD_DIR)/%.S.o: %.S
	@echo "[Kernel] Assembling $<"
	@mkdir -p $(@D)
	$(LOUD)$(X_AS_BIN) $(KERNEL_AS_FLAGS) $< -o $@

$(BUILD_DIR)/%.cpp.o: %.cpp
	@echo "[Kernel] Compiling $<"
	@mkdir -p $(@D)
	$(LOUD)$(X_CXX_BIN) $(KERNEL_CXX_FLAGS) -c $< -o $@
