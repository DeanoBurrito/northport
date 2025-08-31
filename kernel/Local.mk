KERNEL_CXX_SRCS += core/Clock.cpp core/Config.cpp core/CppRuntime.cpp \
	core/Ipl.cpp core/Logging.cpp core/PageAccess.cpp core/PageAlloc.cpp core/Panic.cpp \
	core/Scheduler.cpp core/Smp.cpp core/Str.cpp core/Wait.cpp \
	debugger/Debugger.cpp debugger/ProtocolGdb.cpp \
	entry/BringUp.cpp \
	io/Continuation.cpp io/Packet.cpp io/Str.cpp \
	Scrap.cpp \
	$(BAKED_CONSTANTS_FILE) $(addprefix np-syslib/, $(LIB_SYSLIB_CXX_SRCS))

# TODO: ASAN support
ifeq ($(ENABLE_KERNEL_UBSAN), yes)
	KERNEL_CXX_SRCS += cpp/UBSan.cpp
	KERNEL_CXX_FLAGS += -fsanitize=undefined
endif

ifeq ($(KERNEL_BOOT_PROTOCOL), limine)
	KERNEL_CXX_SRCS += entry/Limine.cpp
else ifeq ($(KERNEL_BOOT_PROTOCOL), crow)
	KERNEL_CXX_SRCS += entry/Crow.cpp
else
$(error "Unknown boot protocol: $(KERNEL_BOOT_PROTOCOL), build aborted.")
endif

BAKED_CONSTANTS_FILE = BakedConstants.cpp
UNITY_SOURCE_FILE = $(BUILD_DIR)/kernel/GeneratedUnitySource.cpp
KERNEL_LD_SCRIPT = kernel/$(PLAT_DIR)/Linker.lds
KERNEL_OBJS = $(patsubst %.S, $(BUILD_DIR)/kernel/%.S.$(KERNEL_CXX_FLAGS_HASH).o, $(KERNEL_AS_SRCS)) 
ifeq ($(KERNEL_UNITY_BUILD), yes)
	KERNEL_OBJS += $(patsubst %.cpp, $(UNITY_SOURCE_FILE).$(KERNEL_CXX_FLAGS_HASH).o, $(UNITY_SOURCE_FILE))
	KERNEL_CXX_FLAGS += -Ikernel -Ilibs
else
	KERNEL_OBJS += $(patsubst %.cpp, $(BUILD_DIR)/kernel/%.cpp.$(KERNEL_CXX_FLAGS_HASH).o, $(KERNEL_CXX_SRCS))
endif

$(KERNEL_TARGET): $(KERNEL_OBJS) $(KERNEL_LD_SCRIPT)
	@printf "$(C_BLUE)[Kernel]$(C_RST) Linking ...\n"
	$(LOUD)$(X_LD_BIN) $(KERNEL_OBJS) $(KERNEL_LD_FLAGS) -T $(KERNEL_LD_SCRIPT) -o $(KERNEL_TARGET)
	@printf "$(C_BLUE)[Kernel]$(C_RST) $(C_GREEN)Done.$(C_RST)\n"

.PHONY: $(UNITY_SOURCE_FILE)
$(UNITY_SOURCE_FILE): kernel/$(BAKED_CONSTANTS_FILE)
	@mkdir -p $(@D)
	$(file > $(UNITY_SOURCE_FILE))
	$(foreach S, $(KERNEL_CXX_SRCS), $(shell printf "#include <$S>\n" >> $(UNITY_SOURCE_FILE)))

$(UNITY_SOURCE_FILE).$(KERNEL_CXX_FLAGS_HASH).o: $(UNITY_SOURCE_FILE)
	@printf "$(C_BLUE)[Kernel]$(C_RST) Compiling unity build source file: $<\n"
	@mkdir -p $(@D)
	$(LOUD)$(X_CXX_BIN) $(KERNEL_CXX_FLAGS) -c $< -o $@

.PHONY: kernel/$(BAKED_CONSTANTS_FILE)
kernel/$(BAKED_CONSTANTS_FILE):
	@printf "$(C_BLUE)[Kernel]$(C_RST) Creating source file for build-time kernel constants\n"
	@mkdir -p $(@D)
	@printf "#include <EntryPrivate.hpp>\n \
		namespace Npk \n\
		{ \n\
			const char* gitHash = \"$(shell git rev-parse HEAD)\"; \n\
			const bool gitDirty = $(shell git diff-index --quiet HEAD --) $(.SHELLSTATUS); \n\
			const char* compileFlags = \"$(KERNEL_CXX_FLAGS)\"; \n\
			const size_t versionMajor = $(KERNEL_VER_MAJOR); \n\
			const size_t versionMinor = $(KERNEL_VER_MINOR); \n\
			const size_t versionRev = $(KERNEL_VER_REVISION); \n\
		}" > kernel/$(BAKED_CONSTANTS_FILE)

$(BUILD_DIR)/kernel/%.S.$(KERNEL_CXX_FLAGS_HASH).o: kernel/%.S
	@printf "$(C_BLUE)[Kernel]$(C_RST) Assembling: $<\n"
	@mkdir -p $(@D)
	$(LOUD)$(X_AS_BIN) $(KERNEL_AS_FLAGS) $< -o $@

$(BUILD_DIR)/kernel/np-syslib/%.cpp.$(KERNEL_CXX_FLAGS_HASH).o: libs/np-syslib/%.cpp
	@printf "$(C_BLUE)[Kernel]$(C_RST) Compiling: $<\n"
	@mkdir -p $(@D)
	$(LOUD)$(X_CXX_BIN) $(KERNEL_CXX_FLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/%.cpp.$(KERNEL_CXX_FLAGS_HASH).o: kernel/%.cpp
	@printf "$(C_BLUE)[Kernel]$(C_RST) Compiling: $<\n"
	@mkdir -p $(@D)
	$(LOUD)$(X_CXX_BIN) $(KERNEL_CXX_FLAGS) -c $< -o $@
