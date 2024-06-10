LOADER_CXX_FLAGS = $(KERNEL_CXX_FLAGS) -I$(PROJ_ROOT_DIR)/libs/np-syslib/include -I$(PROJ_ROOT_DIR)/kernel/include
LOADER_LD_FLAGS = $(KERNEL_LD_FLAGS)

CXX_SRCS = Main.cpp Loader.cpp Memory.cpp
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
