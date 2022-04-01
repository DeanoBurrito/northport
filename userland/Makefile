# This file is the same as libs/Makefile. It's just a glue layer (a dispatcher) from the root makefile
# to the native apps contained here.

SUB_PROJS = startup server-window
TOP_TARGETS = all clean

.PHONY: $(SUB_PROJS) $(TOP_TARGETS)

$(TOP_TARGETS): $(SUB_PROJS)

$(SUB_PROJS):
	@$(MAKE) -C $@ $(MAKECMDGOALS)
