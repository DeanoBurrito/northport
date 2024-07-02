QEMU_CORE = $(QEMU_BASE)

# Build qemu flags. On x86_64 we support booting with bios instead of uefi,
# all other platforms we only support uefi for now.
ifeq ($(CPU_ARCH), x86_64)
ifneq ($(X86_64_RUN_WITH_BIOS), yes)
	QEMU_CORE += $(QEMU_UEFI)
	FW_FILE = $(OVMF_FILE)
endif
else
	QEMU_CORE += $(QEMU_UEFI)
ifneq ($(CPU_ARCH), m68k)
	FW_FILE = $(OVMF_FILE)
endif
endif

.PHONY: run
run: $(ARCH_DEFAULT_TARGET) $(FW_FILE)
	$(LOUD)$(QEMU_CORE) $(QEMU_KVM)

.PHONY: run-kvmless
run-kvmless: $(ARCH_DEFAULT_TARGET) $(FW_FILE) 
	$(LOUD)$(QEMU_CORE) $(QEMU_NO_KVM)

.PHONY: debug
debug: $(ARCH_DEFAULT_TARGET) $(FW_FILE)
	$(LOUD)$(QEMU_CORE) $(QEMU_NO_KVM) $(QEMU_DEBUG)

.PHONY: attach
attach:
	$(LOUD)gdb-multiarch $(KERNEL_FULL_FILEPATH) -ex "target remote :1234"
