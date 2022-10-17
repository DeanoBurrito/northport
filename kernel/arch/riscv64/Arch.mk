CXX_SRCS += $(ARCH_DIR)/Init.cpp $(ARCH_DIR)/Cpu.cpp $(ARCH_DIR)/Paging.cpp \
	$(ARCH_DIR)/Sbi.cpp $(ARCH_DIR)/Interrupts.cpp

AS_SRCS += $(ARCH_DIR)/Entry.S $(ARCH_DIR)/SmpEntry.S $(ARCH_DIR)/Trap.S
