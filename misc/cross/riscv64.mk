KERNEL_CXX_FLAGS += -mcmodel=medlow -mno-relax -DNPK_ARCH_RISCV64
DRIVER_CXX_FLAGS += -mcmodel=medany -mno-relax -DNPK_ARCH_RISCV64
SYSLIB_CXX_FLAGS += -mcmodel=medany -mno-relax -DNPK_ARCH_RISCV64

KERNEL_LD_FLAGS += --no-relax

ARCH_DEFAULT_TARGET = iso
KERNEL_BOOT_PROTOCOL = limine
UEFI_BOOT_NAME = BOOTRISCV64.EFI

QEMU_BASE = qemu-system-$(CPU_ARCH) -machine virt \
	-cpu rv64 -smp 4 -m 256M \
	-device virtio-scsi-pci,id=scsi -device scsi-cd,drive=cd0 \
	-drive id=cd0,format=raw,file=$(ISO_FULL_FILEPATH) \
	-serial mon:stdio -device ramfb -device qemu-xhci -device usb-kbd \
	-M aia=aplic-imsic
QEMU_KVM = 
QEMU_NO_KVM =
QEMU_DEBUG = -s -S -no-reboot -no-shutdown
QEMU_UEFI = -drive if=pflash,format=raw,file=$(OVMF_FILE),unit=0
