TARGET = np-syslib

CXX_FLAGS = -Wall -Wextra -std=c++17 -fno-rtti -fno-exceptions -fno-unwind-tables \
	-fsized-deallocation -fno-asynchronous-unwind-tables -Iinclude -ffreestanding

CXX_SRCS = Memory.cpp String.cpp UnitConverter.cpp Time.cpp Random.cpp NanoPrintf.cpp \
	formats/Tar.cpp formats/Url.cpp formats/Elf.cpp

include $(PROJ_ROOT_DIR)/misc/BuildCommon.mk

# Syslib is a bit special in that we also build a version for the kernel.
# We have separate build + clean recipes for this, and it gets an alternate 
# build dir + compiler flags.
# All the recipes and variables below here are dealing with the kernel version.
K_TARGET = $(LIBS_OUTPUT_DIR)/libk$(TARGET).a
K_BUILD_DIR = k$(BUILD_DIR)
K_OBJS = $(patsubst %.cpp, $(K_BUILD_DIR)/%.cpp.o, $(CXX_SRCS))

all: $(TARGET_STATIC)

.PHONY: all-kernel clean-kernel
all-kernel: $(K_TARGET)

$(K_TARGET): $(K_OBJS)
	@printf "$(C_BLUE)[$(TARGET)-k]$(C_RST) Creating static library ...\r\n"
	$(LOUD)mkdir -p $(@D)
	$(LOUD)$(X_AR_BIN) -rcs $(K_TARGET) $(K_OBJS)
	@printf "$(C_BLUE)[$(TARGET)-k]$(C_RST) $(C_GREEN)Done.$(C_RST)\r\n"

clean-kernel:
	@printf "$(C_BLUE)[$(TARGET)-k]$(C_RST) Cleaning build files ...\r\n"
	$(LOUD)-rm -r $(K_BUILD_DIR)
	$(LOUD)-rm -r $(K_TARGET)
	@printf "$(C_BLUE)[$(TARGET)-k]$(C_RST) $(C_GREEN)Done.$(C_RST)\r\n"

$(K_BUILD_DIR)/%.cpp.o: %.cpp
	@printf "$(C_BLUE)[$(TARGET)-k]$(C_RST) Compiling: $<\r\n"
	$(LOUD)mkdir -p $(@D)
	$(LOUD)$(X_CXX_BIN) $(KERNEL_CXX_FLAGS) $(SYSLIB_CXX_FLAGS) -c $< -o $@

