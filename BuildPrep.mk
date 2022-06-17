#this file just assembles global and debug flags for the build. It's more of a dumping ground to get this out of the main makefile.

ifeq ($(INCLUDE_DEBUG_INFO), true)
	CXX_DEBUG_FLAGS += -g
endif

ifeq ($(ENABLE_DEBUGCON_LOGGING), true)
	CXX_DEBUG_FLAGS += -DNORTHPORT_ENABLE_DEBUGCON_LOG_AT_BOOT
endif
