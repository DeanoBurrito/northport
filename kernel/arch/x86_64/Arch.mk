CXX_SRCS += $(ARCH_DIR)/Init.cpp $(ARCH_DIR)/Cpuid.cpp $(ARCH_DIR)/Hat.cpp \
	$(ARCH_DIR)/Idt.cpp $(ARCH_DIR)/Apic.cpp $(ARCH_DIR)/Timers.cpp $(ARCH_DIR)/Platform.cpp

AS_SRCS += $(ARCH_DIR)/Trap.S $(ARCH_DIR)/Switch.S
