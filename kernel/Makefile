#name of our target file
TARGET = $(KERNEL_FULL_FILEPATH)

CXX_INCLUDE_DIRS = -Iinclude -I$(SYSLIB_INCLUDE_DIR)
CXX_FLAGS = $(CXX_GLOBAL_FLAGS) $(CXX_INCLUDE_DIRS) \
	-Wall -Wextra  -fpie -fno-stack-protector -fno-omit-frame-pointer -ffreestanding \
	-mno-red-zone -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-3dnow \
	-std=c++17 -fno-rtti -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables
LD_LIBS = -L$(shell dirname $(SYSLIB_FULL_FILEPATH)) -lsyslib
LD_SCRIPT = arch/$(CPU_ARCH)/linker.lds
LD_FLAGS = $(LD_LIBS) \
	 -nostdlib -zmax-page-size=0x1000 -static -pie --no-dynamic-linker -ztext
ASM_FLAGS = $(GLOBAL_ASM_FLAGS)

CXX_SRCS = BootStuff.cpp KernelMain.cpp InitTasks.cpp Log.cpp Logf.cpp Panic.cpp \
	memory/PhysicalMemory.cpp memory/Paging.cpp memory/KernelHeap.cpp memory/New.cpp \
	acpi/AcpiTables.cpp \
	drivers/DriverManager.cpp drivers/BuiltInDrivers.cpp \
	devices/LApic.cpp devices/IoApic.cpp devices/SimpleFramebuffer.cpp devices/Ps2Controller.cpp devices/Ps2Keyboard.cpp \
	devices/Ps2Mouse.cpp devices/Keyboard.cpp devices/8254Pit.cpp devices/SystemClock.cpp devices/PciBridge.cpp \
	devices/pci/BochsGraphicsAdaptor.cpp \
	gfx/GraphicsPrimitives.cpp gfx/BakedGfxResources.cpp gfx/TerminalFramebuffer.cpp \
	scheduling/Thread.cpp scheduling/Scheduler.cpp \
	sanitizers/UBSanitizer.cpp
	
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
	@echo "Done."

include Overrides.mk

$(BUILD_DIR)/%.s.o: %.s
	@echo "[Kernel] Assembling source: $<"
	@mkdir -p $(shell dirname $@)
	@$(ASM) $(ASM_FLAGS) $< -o $@

$(BUILD_DIR)/%.cpp.o: %.cpp
	@echo "[Kernel] Compiling CPP source: $<"
	@mkdir -p $(shell dirname $@)
	@$(CXX) $(CXX_FLAGS) -c $< -o $@
