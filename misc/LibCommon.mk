# Common makefile targets for libraries to use, if they dont need a custom implementation.
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
	$(LOUD)mkdir -p $(@D)
	$(LOUD)$(X_AR_BIN) -rcs $(TARGET_STATIC) $(OBJS)
	$(LOUD)echo "[$(TARGET)] Static library created."

clean:
	@echo "[$(TARGET)] Cleaning build dir ..."
	$(LOUD)-rm -r $(BUILD_DIR)
	$(LOUD)-rm -r $(TARGET_STATIC)
	@echo "[$(TARGET)] Done."

$(BUILD_DIR)/%.cpp.o: %.cpp
	@echo "[$(TARGET)] Compiling C++ source: $<"
	$(LOUD)mkdir -p $(@D)
	$(LOUD)$(X_CXX_BIN) $(CXX_FLAGS) -c $< -o $@

