CXX_SRCS += $(ARCH_DIR)/Cpu.cpp $(ARCH_DIR)/Gdt.cpp $(ARCH_DIR)/Idt.cpp $(ARCH_DIR)/InterruptDispatch.cpp \
	$(ARCH_DIR)/StackTrace.cpp $(ARCH_DIR)/Tss.cpp $(ARCH_DIR)/ApBoot.cpp $(ARCH_DIR)/ArchEntry.cpp \
	$(ARCH_DIR)/ArchPlatform.cpp

ASM_SRCS += $(ARCH_DIR)/asm/Trap.s

build/arch/x86_64/ApBoot.cpp.o: build/arch/x86_64/asm/ApTrampoline.s.temp

build/arch/x86_64/asm/ApTrampoline.s.temp: arch/x86_64/asm/ApTrampoline.s
	@echo "[Kernel] Assembling AP trampoline ..."
	@mkdir -p build/arch/x86_64/asm
	@$(ASM) $(ASM_FLAGS) arch/x86_64/asm/ApTrampoline.s -o build/arch/x86_64/asm/ApTrampoline.s.temp
	@objcopy -O binary -j .text build/arch/x86_64/asm/ApTrampoline.s.temp build/arch/x86_64/asm/ApTrampoline.s.o
