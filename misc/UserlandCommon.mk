# Similar to LibCommon.mk, this file provides some common utilities for compiling northport applications
# NOTE: these expect the following variables (most others will come from the root makefile):
# CXX_FLAGS = c++ compiler flags
# CXX_SRCS = c++ source files
# TARGET = application name

OBJS = $(patsubst %.cpp, $(BUILD_DIR)/%.cpp.o, $(CXX_SRCS))

.PHONY: clean

clean:
	@echo "[$(TARGET)] Cleaning build dir ..."
	@-rm -r $(BUILD_DIR)
	@-rm -r $(USERLAND_OUTPUT_DIR)/$(TARGET).elf
	@echo "[$(TARGET)] Done."

$(BUILD_DIR)/%.cpp.o: %.cpp
	@echo "[$(TARGET)] Compiling C++ source: $<"
	@mkdir -p $(shell dirname $@)
	@$(CXX) $(CXX_FLAGS) -c $< -o $@

