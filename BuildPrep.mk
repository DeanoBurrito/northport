#this file just assembles global and debug flags for the build. It's more of a dumping ground to get this out of the main makefile.

ifeq ($(INCLUDE_DEBUG_INFO), true)
	CXX_GLOBAL_FLAGS += -g
endif

ifeq ($(ENABLE_DEBUGCON_LOGGING), true)
	CXX_KERNEL_FLAGS += -DNORTHPORT_LOG_DEBUGCON_ENABLE_EARLY
endif

ifeq ($(ENABLE_FRAMEBUFFER_LOGGING), true)
	CXX_KERNEL_FLAGS += -DNORTHPORT_LOG_FRAMEBUFFER_ENABLE_EARLY
endif

ifeq ($(ENABLE_KERNEL_UBSAN), true)
	CXX_KERNEL_FLAGS += -fsanitize=undefined
endif

ifeq ($(BOOT_WITH_UEFI), true)
	QEMU_EXTRA_FLAGS += -bios /usr/share/ovmf/OVMF.fd
endif
