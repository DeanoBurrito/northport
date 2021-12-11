CXX_FLAGS = $(CXX_GLOBAL_FLAGS) -Iinclude -fno-omit-frame-pointer -ffreestanding -fno-rtti -fno-exceptions -mno-red-zone -Wall -fpie -std=c++17
CXX_SRCS = Memory.cpp String.cpp StringCulture.cpp FormatPrinter.cpp Format.cpp

OBJS = $(patsubst %.cpp, $(BUILD_DIR)/%.cpp.o, $(CXX_SRCS))

all: $(OBJS)
	@echo "[Syslib] Creating static library..."
	@$(AR) -rcs $(SYSLIB_FULL_FILEPATH) $(OBJS)
	@echo "Done."

clean:
	@echo "[Syslib] Cleaning build dir."
	@rm -r $(BUILD_DIR)
	@echo "Done."

$(BUILD_DIR)/%.cpp.o: %.cpp
	@echo "[Syslib] Compiling CPP source: $<"
	@mkdir -p $(shell dirname $@)
	@$(CXX) $(CXX_FLAGS) -c $< -o $@
