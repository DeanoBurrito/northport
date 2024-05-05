X86_64_FLAGS = -mno-red-zone -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-3dnow
KERNEL_CXX_FLAGS += $(X86_64_FLAGS) -mcmodel=kernel
DRIVER_CXX_FLAGS += $(X86_64_FLAGS) -mcmodel=large
DRIVER_C_FLAGS += $(X86_64_FLAGS) -mcmodel=large
SYSLIB_CXX_FLAGS += -mcmodel=large

KERNEL_LD_FLAGS += -ztext

ARCH_DEFAULT_TARGET = iso-hybrid
UEFI_BOOT_NAME = BOOTX64.EFI

QEMU_BASE = qemu-system-x86_64 -machine q35 \
	-smp cores=2,threads=2 -m 256M -cdrom $(ISO_FULL_FILEPATH) \
	-debugcon /dev/stdout -monitor stdio -device virtio-gpu \
	-drive file=nvm-backing.img,format=raw,if=none,id=nvm -device nvme,serial=pls_star,drive=nvm
QEMU_KVM = --enable-kvm -cpu host
QEMU_NO_KVM = -cpu qemu64,+smap,+smep
QEMU_DEBUG = -s -S -no-reboot -no-shutdown
QEMU_UEFI = -drive if=pflash,format=raw,file=$(OVMF_FILE),readonly=on
