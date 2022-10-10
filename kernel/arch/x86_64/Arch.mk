CXX_SRCS += $(ARCH_DIR)/Init.cpp $(ARCH_DIR)/Cpu.cpp $(ARCH_DIR)/Paging.cpp $(ARCH_DIR)/Gdt.cpp \
	$(ARCH_DIR)/Idt.cpp $(ARCH_DIR)/Apic.cpp $(ARCH_DIR)/Timers.cpp $(ARCH_DIR)/Smp.cpp

AS_SRCS += $(ARCH_DIR)/Trap.S
