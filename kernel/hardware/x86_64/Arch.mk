KERNEL_CXX_SRCS += $(ARCH_DIR)/Arch.cpp $(ARCH_DIR)/Cpuid.cpp $(ARCH_DIR)/Apic.cpp \
	$(ARCH_DIR)/Mmu.cpp

KERNEL_AS_SRCS += $(ARCH_DIR)/Entry.S $(ARCH_DIR)/Unsafe.S $(ARCH_DIR)/Spinup.S
