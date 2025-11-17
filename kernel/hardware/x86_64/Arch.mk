KERNEL_CXX_SRCS += $(ARCH_DIR)/ApBringup.cpp $(ARCH_DIR)/Arch.cpp \
	$(ARCH_DIR)/Cpuid.cpp $(ARCH_DIR)/Debug.cpp $(ARCH_DIR)/Entry.cpp \
	$(ARCH_DIR)/MachineCheck.cpp $(ARCH_DIR)/Hpet.cpp $(ARCH_DIR)/Uart.cpp \
	$(ARCH_DIR)/LocalApic.cpp $(ARCH_DIR)/Mmu.cpp $(ARCH_DIR)/Msr.cpp \
	$(ARCH_DIR)/PvClock.cpp $(ARCH_DIR)/TrapFrame.cpp $(ARCH_DIR)/Tsc.cpp \
	hardware/common/AcpiTimer.cpp hardware/common/Ns16550.cpp

KERNEL_AS_SRCS += $(ARCH_DIR)/Entry.S $(ARCH_DIR)/ExceptionAwareCall.S \
	$(ARCH_DIR)/Spinup.S $(ARCH_DIR)/Switch.S
