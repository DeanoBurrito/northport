KERNEL_CXX_FLAGS += -mno-red-zone -mno-80387 -mno-mmx -mno-sse -mno-sse2 \
	-mno-3dnow -mcmodel=kernel

KERNEL_LD_FLAGS += -ztext

ARCH_DEFAULT_TARGET = iso

QEMU_BASE = qemu-system-x86_64 -machine q35 \
	-smp cores=4 -m 512M -cdrom $(ISO_FULL_FILEPATH) \
	-debugcon /dev/stdout -monitor stdio
QEMU_KVM = --enable-kvm -cpu host
QEMU_NO_KVM = -cpu qemu64,+smap,+smep
QEMU_DEBUG = -s -S -no-reboot -no-shutdown
QEMU_UEFI = -bios /usr/share/ovmf/OVMF.fd
