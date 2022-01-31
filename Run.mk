#stuff for running and debugging the os - not needed to just build an image

QEMU_BIN = qemu-system-$(CPU_ARCH)
QEMU_FLAGS = -machine q35 -smp cores=4 -monitor stdio -m 256M -cdrom $(ISO_TARGET) -debugcon /dev/stdout
QEMU_RUN_FLAGS = --enable-kvm
QEMU_DEBUG_FLAGS = -s -S

DBG_BIN = gdb
DBG_FLAGS = $(KERNEL_FULL_FILEPATH) -ex "target remote :1234"

#alias to select what we want to build before running/debugging
pre-run-target: all

run: pre-run-target
	@$(QEMU_BIN) $(QEMU_FLAGS) $(QEMU_RUN_FLAGS)

debug: pre-run-target
	@$(QEMU_BIN) $(QEMU_FLAGS) $(QEMU_DEBUG_FLAGS)

attach:
	@$(DBG_BIN) $(DBG_FLAGS)

debug-hang:
	@$(QEMU_BIN) $(QEMU_FLAGS) $(QEMU_DEBUG_FLAGS) -no-reboot -no-shutdown
