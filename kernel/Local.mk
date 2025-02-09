KERNEL_CXX_SRCS += Entry.cpp Exit.cpp KernelThread.cpp Panic.cpp \
	core/Clock.cpp core/Config.cpp core/Event.cpp core/WiredHeap.cpp core/IntrRouter.cpp \
	core/Log.cpp core/Pmm.cpp core/RunLevels.cpp core/Scheduler.cpp core/Smp.cpp \
	cpp/Stubs.cpp \
	$(BAKED_CONSTANTS_FILE) $(addprefix syslib/, $(LIB_SYSLIB_CXX_SRCS)) \
	services/AcpiTables.cpp services/BadSwap.cpp services/MagicKeys.cpp \
	services/Program.cpp services/SymbolStore.cpp services/Vmm.cpp services/VmPagers.cpp

ifeq ($(ENABLE_KERNEL_ASAN), yes)
	KERNEL_CXX_SRCS += cpp/Asan.cpp

	KERNEL_CXX_FLAGS += -fsanitize=kernel-address -DNPK_HAS_KASAN
	ifeq ($(TOOLCHAIN), clang)
		KERNEL_CXX_FLAGS += -asan-mapping-offset=0xdfffe00000000000
	endif
endif

ifeq ($(ENABLE_KERNEL_UBSAN), yes)
	KERNEL_CXX_SRCS += cpp/UBSan.cpp
	KERNEL_CXX_FLAGS += -fsanitize=undefined
endif

ifeq ($(KERNEL_BOOT_PROTOCOL), limine)
	KERNEL_CXX_SRCS += interfaces/loader/Limine.cpp
else ifeq ($(KERNEL_BOOT_PROTOCOL), crow)
	KERNEL_CXX_SRCS += interfaces/loader/Crow.cpp
else
$(error "Unknown boot protocol: $(KERNEL_BOOT_PROTOCOL), build aborted.")
endif

BAKED_CONSTANTS_FILE = interfaces/intra/BakedConstants.cpp
KERNEL_LD_SCRIPT = kernel/$(ARCH_DIR)/Linker.lds
KERNEL_OBJS = $(patsubst %.S, $(BUILD_DIR)/kernel/%.S.$(KERNEL_CXX_FLAGS_HASH).o, $(KERNEL_AS_SRCS)) \
	$(patsubst %.cpp, $(BUILD_DIR)/kernel/%.cpp.$(KERNEL_CXX_FLAGS_HASH).o, $(KERNEL_CXX_SRCS))

$(KERNEL_TARGET): $(KERNEL_OBJS) $(KERNEL_LD_SCRIPT)
	@printf "$(C_BLUE)[Kernel]$(C_RST) Linking ...\n"
	$(LOUD)$(X_LD_BIN) $(KERNEL_OBJS) $(KERNEL_LD_FLAGS) -T $(KERNEL_LD_SCRIPT) -o $(KERNEL_TARGET)
	@printf "$(C_BLUE)[Kernel]$(C_RST) $(C_GREEN)Done.$(C_RST)\n"

.PHONY: kernel/$(BAKED_CONSTANTS_FILE)
kernel/$(BAKED_CONSTANTS_FILE):
	@printf "$(C_BLUE)[Kernel]$(C_RST) Creating source file for build-time kernel constants\n"
	@mkdir -p $(@D)
	@printf "#include <interfaces/intra/BakedConstants.h>\n \
		namespace Npk \n\
		{ \n\
			const char* targetArchStr = \"$(CPU_ARCH)\"; \n\
			const char* gitCommitHash = \"$(shell git rev-parse HEAD)\"; \n\
			const char* gitCommitShortHash = \"$(shell git rev-parse --short HEAD)\"; \n\
			const bool gitCommitDirty = $(shell git diff-index --quiet HEAD --) $(.SHELLSTATUS); \n\
			const size_t versionMajor = $(KERNEL_VER_MAJOR); \n\
			const size_t versionMinor = $(KERNEL_VER_MINOR); \n\
			const size_t versionRev = $(KERNEL_VER_REVISION); \n\
			const char* toolchainUsed = \"$(TOOLCHAIN)\"; \n\
		}" > kernel/$(BAKED_CONSTANTS_FILE)

$(BUILD_DIR)/kernel/%.S.$(KERNEL_CXX_FLAGS_HASH).o: kernel/%.S
	@printf "$(C_BLUE)[Kernel]$(C_RST) Assembling: $<\n"
	@mkdir -p $(@D)
	$(LOUD)$(X_AS_BIN) $(KERNEL_AS_FLAGS) $< -o $@

$(BUILD_DIR)/kernel/syslib/%.cpp.$(KERNEL_CXX_FLAGS_HASH).o: libs/np-syslib/%.cpp
	@printf "$(C_BLUE)[Kernel]$(C_RST) Compiling: $<\n"
	@mkdir -p $(@D)
	$(LOUD)$(X_CXX_BIN) $(KERNEL_CXX_FLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/%.cpp.$(KERNEL_CXX_FLAGS_HASH).o: kernel/%.cpp
	@printf "$(C_BLUE)[Kernel]$(C_RST) Compiling: $<\n"
	@mkdir -p $(@D)
	$(LOUD)$(X_CXX_BIN) $(KERNEL_CXX_FLAGS) -c $< -o $@
