# This makefile is a glue layer between the top level and individual library makefiles.
# It'll run a make target on all directories listed in SUB_PROJS, as long as 
# target exists in TOP_TARGETS.

SUB_PROJS = np-graphics np-syslib np-syscall np-userland np-gui
TOP_TARGETS = all clean

.PHONY: $(SUB_PROJS) $(TOP_TARGETS)

$(TOP_TARGETS): $(SUB_PROJS)

$(SUB_PROJS):
	@$(MAKE) -C $@ $(MAKECMDGOALS)
