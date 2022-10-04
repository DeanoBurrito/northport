QEMU_CORE = $(QEMU_BASE)
ifeq ($(BOOT_WITH_UEFI), yes)
	QEMU_CORE += $(QEMU_UEFI)
endif

.PHONY: run
run: $(ARCH_DEFAULT_TARGET)
	$(LOUD)$(QEMU_CORE) $(QEMU_KVM)

.PHONY: run-kvmless
run-kvmless: $(ARCH_DEFAULT_TARGET)
	$(LOUD)$(QEMU_CORE) $(QEMU_NO_KVM)

.PHONY: debug
debug: $(ARCH_DEFAULT_TARGET)
	$(LOUD)$(QEMU_CORE) $(QEMU_NO_KVM) $(QEMU_DEBUG)

.PHONY: attach
attach:
	$(LOUD)gdb-multiarch $(KERNEL_FULL_FILEPATH) -ex "target remote :1234"
