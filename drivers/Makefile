ENABLED_DRIVERS = pci qemu nvme uacpi
TOP_TARGETS = all clean

.PHONY: $(ENABLED_DRIVERS) $(TOP_TARGETS)
$(TOP_TARGETS): $(ENABLED_DRIVERS)

$(ENABLED_DRIVERS):
	@$(MAKE) -C $@ $(MAKECMDGOALS)

