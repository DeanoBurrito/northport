# Common makefile targets for libraries to use, if they dont need a custom impl
# NOTE: these expect the following variables (most others will come from the root makefile):
# CXX_FLAGS = c++ compiler flags
# CXX_SRCS = c++ source files
# TARGET = library name

OBJS = $(patsubst %.cpp, $(BUILD_DIR)/%.cpp.o, $(CXX_SRCS))
TARGET_STATIC = $(LIBS_OUTPUT_DIR)/lib$(TARGET).a

.PHONY: clean all

all: $(TARGET_STATIC)

$(TARGET_STATIC): $(OBJS)
	@echo "[$(TARGET)] Creating static library ..."
	@mkdir -p $(shell dirname $(TARGET_STATIC))
	@$(AR) -rcs $(TARGET_STATIC) $(OBJS)
	@echo "[$(TARGET)] Static library created."

clean:
	@echo "[$(TARGET)] Cleaning build dir ..."
	@-rm -r $(BUILD_DIR)
	@-rm -r $(TARGET_STATIC)
	@echo "[$(TARGET)] Done."

$(BUILD_DIR)/%.cpp.o: %.cpp
	@echo "[$(TARGET)] Compiling C++ source: $<"
	@mkdir -p $(shell dirname $@)
	@$(CXX) $(CXX_FLAGS) -c $< -o $@

