
TARGET = np-syslib

CXX_FLAGS = -Wall -Wextra -std=c++17 -fno-rtti -fno-exceptions -fno-unwind-tables \
	-fno-asynchronous-unwind-tables -Iinclude -ffreestanding

CXX_SRCS = Memory.cpp String.cpp UnitConverter.cpp Time.cpp

include $(LIB_COMMON_MK)

# Syslib is a bit special in that we also build a version for the kernel.
# We have separate build + clean recipes for this, and it gets an alternate 
# build dir + compiler flags.
# All the recipes and variables below here are dealing with the kernel version.
K_TARGET = $(LIBS_OUTPUT_DIR)/libk$(TARGET).a
K_BUILD_DIR = k$(BUILD_DIR)
K_OBJS = $(patsubst %.cpp, $(K_BUILD_DIR)/%.cpp.o, $(CXX_SRCS))

all-kernel: $(K_TARGET)

$(K_TARGET): $(K_OBJS)
	@echo "[$(TARGET)] Linking static library (with kernel flags) ..."
	$(LOUD)mkdir -p $(@D)
	$(LOUD)$(X_AR_BIN) -rcs $(K_TARGET) $(K_OBJS)
	@echo "[$(TARGET)] Done."

clean-kernel:
	@echo "[$(TARGET)] Cleaning build files ..."
	$(LOUD)-rm -r $(K_BUILD_DIR)
	$(LOUD)-rm -r $(K_TARGET)
	@echo "[$(TARGET)] Done."

$(K_BUILD_DIR)/%.cpp.o: %.cpp
	@echo "[$(TARGET)] Compiling $< (with kernel flags)"
	$(LOUD)mkdir -p $(@D)
	$(LOUD)$(X_CXX_BIN) $(KERNEL_CXX_FLAGS) -c $< -o $@
