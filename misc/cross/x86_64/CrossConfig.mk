KERNEL_CXX_FLAGS += -mno-red-zone -mno-80387 -mno-mmx -mno-sse -mno-sse2 \
	-mno-3dnow -mcmodel=kernel

KERNEL_LD_FLAGS += -ztext

ARCH_DEFAULT_TARGET = iso

QEMU_BASE = qemu-system-x86_64 -machine q35 \
	-smp cores=4 -m 256M -cdrom $(ISO_FULL_FILEPATH) \
	-debugcon /dev/stdout -monitor stdio -device virtio-gpu
QEMU_KVM = --enable-kvm -cpu host
QEMU_NO_KVM = -cpu qemu64,+smap,+smep
QEMU_DEBUG = -s -S -no-reboot -no-shutdown
# For single-file ovmf use the '-bios' line, for separate code/vars files use the '-drive' lines 
# Don't forget to use a separate copy of the vars file that is user-writable.
QEMU_UEFI = -bios /usr/share/ovmf/OVMF.fd
#QEMU_UEFI = -drive if=pflash,format=raw,file=/usr/share/edk2/ovmf/OVMF_CODE.fd,readonly=on
#QEMU_UEFI += -drive if=pflash,format=raw,file=../cross-tools/OVMF_VARS.fd
