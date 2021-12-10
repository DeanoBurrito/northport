#name of our target file
TARGET = $(KERNEL_FULL_FILEPATH)

CXX_INCLUDE_DIRS = -Iinclude -I$(SYSLIB_INCLUDE_DIR)
CXX_FLAGS = $(CXX_GLOBAL_FLAGS) $(CXX_INCLUDE_DIRS) \
	-fno-stack-protector -fno-omit-frame-pointer -ffreestanding -fno-unwind-tables -fno-asynchronous-unwind-tables \
	-fno-rtti -fno-exceptions -mno-red-zone -Wall -Wextra -std=c++17 -fpie
LD_LIBS = -L$(shell dirname $(SYSLIB_FULL_FILEPATH)) -lsyslib
LD_SCRIPT = arch/$(CPU_ARCH)/linker.lds
LD_FLAGS = $(LD_LIBS) \
	 -nostdlib -zmax-page-size=0x1000 -static -pie --no-dynamic-linker -ztext
ASM_FLAGS = --64

CXX_SRCS = BootStuff.cpp KernelMain.cpp Log.cpp Logf.cpp memory/PhysicalMemory.cpp memory/Paging.cpp memory/KernelHeap.cpp \
	memory/New.cpp acpi/AcpiTables.cpp
ASM_SRCS = 

ARCH_DIR = arch/$(CPU_ARCH)
include $(ARCH_DIR)/local.mk

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
	@echo "Done."

include overrides.mk

$(BUILD_DIR)/%.s.o: %.s
	@echo "[Kernel] Assembling source: $<"
	@mkdir -p $(shell dirname $@)
	@$(ASM) $(ASM_FLAGS) $< -o $@

$(BUILD_DIR)/%.cpp.o: %.cpp
	@echo "[Kernel] Compiling CPP source: $<"
	@mkdir -p $(shell dirname $@)
	@$(CXX) $(CXX_FLAGS) -c $< -o $@
