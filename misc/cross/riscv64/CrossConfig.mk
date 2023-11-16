KERNEL_CXX_FLAGS += -mcmodel=medlow
DRIVER_CXX_FLAGS += -mcmodel=medany
SYSLIB_CXX_FLAGS += -mcmodel=medany

KERNEL_LD_FLAGS +=

ARCH_DEFAULT_TARGET = iso
UEFI_BOOT_NAME = BOOTRISCV64.EFI

QEMU_BASE = qemu-system-$(CPU_ARCH) -machine virt \
	-cpu rv64 -smp 4 -m 512M \
	-device virtio-scsi-pci,id=scsi -device scsi-cd,drive=cd0 \
	-drive id=cd0,format=raw,file=$(ISO_FULL_FILEPATH) \
	-serial mon:stdio -device ramfb -device qemu-xhci -device usb-kbd \
	-M aia=aplic-imsic
QEMU_KVM = 
QEMU_NO_KVM =
QEMU_DEBUG = -s -S -no-reboot -no-shutdown
QEMU_UEFI = -drive if=pflash,format=raw,file=$(OVMF_FILE),readonly=on,unit=1
