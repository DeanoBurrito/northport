CXX_SRCS += $(ARCH_DIR)/Init.cpp $(ARCH_DIR)/Cpu.cpp $(ARCH_DIR)/Hat.cpp \
	$(ARCH_DIR)/Sbi.cpp $(ARCH_DIR)/Interrupts.cpp $(ARCH_DIR)/Timers.cpp \
	$(ARCH_DIR)/Platform.cpp

AS_SRCS += $(ARCH_DIR)/Entry.S $(ARCH_DIR)/SmpEntry.S $(ARCH_DIR)/Trap.S \
	$(ARCH_DIR)/Panic.S $(ARCH_DIR)/Switch.S
