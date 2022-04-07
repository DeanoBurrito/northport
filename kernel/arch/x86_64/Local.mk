CXX_SRCS += $(ARCH_DIR)/Cpu.cpp $(ARCH_DIR)/Gdt.cpp $(ARCH_DIR)/Idt.cpp $(ARCH_DIR)/InterruptDispatch.cpp $(ARCH_DIR)/StackTrace.cpp $(ARCH_DIR)/Tss.cpp \
	$(ARCH_DIR)/ApBoot.cpp

ASM_SRCS += $(ARCH_DIR)/IdtStubs.s

build/arch/x86_64/ApBoot.cpp.o: build_x86_64_trampoline

build_x86_64_trampoline:
	@echo "[Kernel] Assembling AP trampoline ..."
	@mkdir -p $(shell dirname build/arch/x86_64/ApTrampoline.s.o)
	@$(ASM) $(ASM_FLAGS) arch/x86_64/ApTrampoline.s -o build/arch/x86_64/ApTrampoline.s.temp
	@objcopy -O binary -j .text build/arch/x86_64/ApTrampoline.s.temp build/arch/x86_64/ApTrampoline.s.o
