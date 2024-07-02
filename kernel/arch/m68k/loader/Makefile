LOADER_CXX_FLAGS = -Wall -Wextra -fstack-protector-strong -ffreestanding -std=c++17 \
	-fno-rtti -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables -g \
	-m68040 \
	-I$(PROJ_ROOT_DIR)/libs/np-syslib/include -I$(PROJ_ROOT_DIR)/kernel/include
LOADER_LD_FLAGS = -nostdlib -static --no-dynamic-linker

CXX_SRCS = Main.cpp Loader.cpp Memory.cpp Syslib.cpp
AS_SRCS = Entry.S

LD_SCRIPT = Linker.lds
CXX_OBJS = $(patsubst %.cpp, $(BUILD_DIR)/%.cpp.o, $(CXX_SRCS))
AS_OBJS = $(patsubst %.S, $(BUILD_DIR)/%.S.o, $(AS_SRCS))
OBJS = $(AS_OBJS) $(CXX_OBJS)

include $(PROJ_ROOT_DIR)/misc/Formatting.mk

.PHONY: all
all: $(LOADER_FULL_FILEPATH)

$(LOADER_FULL_FILEPATH): $(OBJS) $(LD_SCRIPT)
	@printf "$(C_BLUE)[Loader]$(C_RST) Linking ...\r\n"
	$(LOUD)$(X_LD_BIN) $(OBJS) $(LOADER_LD_FLAGS) -T $(LD_SCRIPT) -o $(LOADER_FULL_FILEPATH)
	@printf "$(C_BLUE)[Loader]$(C_RST) $(C_GREEN)Done.$(C_RST)\r\n"

.PHONY: clean
clean:
	@printf "$(C_BLUE)[Loader]$(C_RST) Cleaning build files ...\r\n"
	$(LOUD)-rm -r $(BUILD_DIR)
	@printf "$(C_BLUE)[Loader]$(C_RST) $(C_GREEN)Done.$(C_RST)\r\n"

$(BUILD_DIR)/%.S.o: %.S
	@printf "$(C_BLUE)[Loader]$(C_RST) Assembling: $<\r\n"
	@mkdir -p $(@D)
	$(LOUD)$(X_AS_BIN) $(LOADER_AS_FLAGS) $< -o $@

$(BUILD_DIR)/%.cpp.o: %.cpp
	@printf "$(C_BLUE)[Loader]$(C_RST) Compiling: $<\r\n"
	@mkdir -p $(@D)
	$(LOUD)$(X_CXX_BIN) $(LOADER_CXX_FLAGS) -c $< -o $@

$(BUILD_DIR)/Loader.cpp.o: Loader.cpp $(BUILD_DIR)/kernel.elf
	@printf "$(C_BLUE)[Loader]$(C_RST) Compiling (with embedded kernel): $<\r\n"
	@mkdir -p $(@D)
	$(LOUD)$(X_CXX_BIN) $(LOADER_CXX_FLAGS) -c $< -o $@

