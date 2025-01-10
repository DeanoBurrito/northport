CXX_SRCS += $(ARCH_DIR)/Init.cpp $(ARCH_DIR)/Interrupts.cpp $(ARCH_DIR)/Misc.cpp \
	$(ARCH_DIR)/Hat.cpp $(ARCH_DIR)/Cpuid.cpp $(ARCH_DIR)/Apic.cpp $(ARCH_DIR)/Timers.cpp

AS_SRCS += $(ARCH_DIR)/Trap.S $(ARCH_DIR)/Switch.S $(ARCH_DIR)/Unsafe.S
