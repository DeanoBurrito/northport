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
		KERNEL_CXX_FLAGS += -DNPK_X86_DEBUGCON_ENABLED
	endif
endif

ifeq ($(CPU_ARCH), riscv64)
	ifneq ($(RV64_ASSUME_UART), no)
		KERNEL_CXX_FLAGS += -DNP_RISCV64_ASSUME_SERIAL=$(RV64_ASSUME_UART)
	endif
endif

ifeq ($(CPU_ARCH), m68k)
	ifneq ($(M68K_ASSUME_UART), no)
		KERNEL_CXX_FLAGS += -DNP_M68K_ASSUME_TTY=$(M68K_ASSUME_UART)
	endif
endif
