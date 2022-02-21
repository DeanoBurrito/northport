# the name of this project/library
TARGET = np-bareones

# any compiler flags, dont forget to include the global flags.
CXX_FLAGS = $(CXX_GLOBAL_FLAGS) -Iinclude -I$(LIBS_DIR)/np-example-lib \
	-fno-omit-frame-pointer -fno-stack-protector -ffreestanding -fno-rtti -fno-exceptions -mno-red-zone -Wall -Wextra \
	-fpie -std=c++17 -mno-80387 -mno-sse -mno-sse2 -mno-3dnow

# source files
CXX_SRCS =

#LIB_COMMON_MK contains a default `make clean` implementation,
#as well as a fairly normal build rule for c++ source files.
include $(LIB_COMMON_MK)

.PHONY: all

#all thats left to implement is `make all`, left to the user as it might require custom processing.
all: $(OBJS) 
	@echo "[$(TARGET)] Creating static library ..."
	@mkdir -p $(LIBS_OUTPUT_DIR)
	@$(AR) -rcs $(LIBS_OUTPUT_DIR)/lib$(TARGET).a $(OBJS)
	@echo "Done."

#note that there is an empty `include/` in this folder. Not necessary, 
#but any publicly-visible header should be kept here.
