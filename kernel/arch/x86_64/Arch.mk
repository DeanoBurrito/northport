KERNEL_CXX_SRCS += $(ARCH_DIR)/Init.cpp $(ARCH_DIR)/Interrupts.cpp $(ARCH_DIR)/Misc.cpp \
	$(ARCH_DIR)/Hat.cpp $(ARCH_DIR)/Cpuid.cpp $(ARCH_DIR)/Apic.cpp $(ARCH_DIR)/Timers.cpp \
	$(ARCH_DIR)/Switch.cpp

KERNEL_AS_SRCS += $(ARCH_DIR)/Entry.S $(ARCH_DIR)/Unsafe.S
