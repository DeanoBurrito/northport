#this file just assembles global and debug flags for the build. It's more of a dumping ground to get this out of the main makefile.

ifeq ($(INCLUDE_DEBUG_INFO), true)
	CXX_GLOBAL_FLAGS += -g
endif

ifeq ($(ENABLE_DEBUGCON_LOGGING), true)
	CXX_GLOBAL_FLAGS += -DNORTHPORT_ENABLE_DEBUGCON_LOG_AT_BOOT
endif

ifeq ($(ENABLE_KERNEL_UBSAN), true)
	CXX_KERNEL_FLAGS += -fsanitize=undefined
endif
