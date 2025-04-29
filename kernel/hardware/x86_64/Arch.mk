KERNEL_CXX_SRCS += $(ARCH_DIR)/Arch.cpp $(ARCH_DIR)/Cpuid.cpp $(ARCH_DIR)/EarlyLogging.cpp \
	$(ARCH_DIR)/LocalApic.cpp $(ARCH_DIR)/Mmu.cpp

KERNEL_AS_SRCS += $(ARCH_DIR)/Entry.S $(ARCH_DIR)/Spinup.S $(ARCH_DIR)/Switch.S
