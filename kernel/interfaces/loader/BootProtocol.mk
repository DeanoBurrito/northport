ifeq ($(KERNEL_BOOT_PROTOCOL), limine)
	CXX_SRCS += interfaces/loader/Limine.cpp
else ifeq ($(KERNEL_BOOT_PROTOCOL), crow)
	CXX_SRCS += interfaces/loader/Crow.cpp
else
$(error "Unknown boot protocol: $(KERNEL_BOOT_PROTOCOL), build aborted.")
endif
