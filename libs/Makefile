SUB_PROJS = np-syslib np-driverlib
TOP_TARGETS = all clean

.PHONY: $(SUB_PROJS) $(TOP_TARGETS)
$(TOP_TARGETS): $(SUB_PROJS)

$(SUB_PROJS):
	@$(MAKE) -C $@ $(MAKECMDGOALS)
