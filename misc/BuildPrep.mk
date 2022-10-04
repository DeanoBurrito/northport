ifeq ($(QUIET_BUILD), yes)
	export LOUD = @
	export LOUD_REDIRECT = 2>/dev/null
else
	export LOUD =
	export LOUD_REDIRECT =
endif

ifeq ($(INCLUDE_TERMINAL_BG), yes)
	KERNEL_CXX_FLAGS += -DNP_INCLUDE_TERMINAL_BG
endif

ifeq ($(CPU_ARCH), x86_64)
	ifeq ($(X86_64_ENABLE_DEBUGCON_E9), yes)
		KERNEL_CXX_FLAGS += -DNP_X86_64_E9_ALLOWED
	endif
endif

ifeq ($(CPU_ARCH), riscv64)
	ifeq ($(RV64_NO_RDTIME), yes)
		KERNEL_CXX_FLAGS += -DNP_RV64_NO_RDTIME_INSTR
	endif
endif
