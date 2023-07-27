KERNEL_CXX_FLAGS += -mno-red-zone -mno-80387 -mno-mmx -mno-sse -mno-sse2 \
	-mno-3dnow -mcmodel=kernel

KERNEL_LD_FLAGS += -ztext

ARCH_DEFAULT_TARGET = iso-hybrid
UEFI_BOOT_NAME = BOOTX64.EFI

QEMU_BASE = qemu-system-x86_64 -machine q35 \
	-smp cores=4,threads=2 -m 256M -cdrom $(ISO_FULL_FILEPATH) \
	-debugcon /dev/stdout -monitor stdio -device virtio-gpu
QEMU_KVM = --enable-kvm -cpu host
QEMU_NO_KVM = -cpu qemu64,+smap,+smep
QEMU_DEBUG = -s -S -no-reboot -no-shutdown --enable-kvm -cpu host
QEMU_UEFI = -drive if=pflash,format=raw,file=$(OVMF_FILE),readonly=on
