TESTS_TARGET = $(BUILD_DIR)/tests.elf
TESTS_SRCS += syslib/Flags.cpp syslib/Maths.cpp
TESTS_CXX_FLAGS = -O2 -I$(CATCH2_DIR)/extras -Ilibs/np-syslib/include

TESTS_CXX_FLAGS_HASH = $(strip $(shell echo $(TESTS_CXX_FLAGS) | sha256sum | cut -d " " -f1))
CATCH2_DIR = $(VENDOR_CACHE_DIR)/catch2

build-tests: $(CATCH2_DIR) $(TESTS_TARGET)
tests: build-tests
	./$(TESTS_TARGET)

$(TESTS_TARGET): $(addprefix tests/, $(TESTS_SRCS))
	@printf "$(C_BLUE)[Tests]$(C_RST) Building ...\n"
	$(LOUD)$(CXX) $(TESTS_CXX_FLAGS) $(CATCH2_DIR)/extras/catch_amalgamated.cpp \
		$(addprefix tests/, $(TESTS_SRCS)) -o $@
	@printf "$(C_BLUE)[Tests]$(C_RST) $(C_GREEN)Done.$(C_RST)\n"

$(CATCH2_DIR):
	$(LOUD)-rm -rf $(CATCH2_DIR)
	$(LOUD)git clone https://github.com/catchorg/Catch2.git --branch=v3.8.0 \
		--depth 1 $(CATCH2_DIR)
	@printf "$(C_YELLOW)[Cache]$(C_RST) Catch2 repo cloned to local cache.\r\n"
