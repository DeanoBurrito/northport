TARGET = np-graphics

CXX_FLAGS = $(CXX_GLOBAL_FLAGS) -Iinclude -I$(LIBS_DIR)/np-syscall/include -I$(LIBS_DIR)/np-syslib/include \
	-fno-omit-frame-pointer -fno-stack-protector -ffreestanding -fno-rtti -fno-exceptions -mno-red-zone -Wall -Wextra \
	-fpie -std=c++17 -mno-80387 -mno-sse -mno-sse2 -mno-3dnow

CXX_SRCS = BakedData.cpp LinearFramebuffer.cpp Colour.cpp TerminalFramebuffer.cpp \
	formats/GenericImage.cpp formats/Qoi.cpp

include $(LIB_COMMON_MK)

.PHONY: all

all: $(OBJS)
	@echo "[$(TARGET)] Creating static library ..."
	@mkdir -p $(LIBS_OUTPUT_DIR)
	@$(AR) -rcs $(LIBS_OUTPUT_DIR)/lib$(TARGET).a $(OBJS)
	@echo "[$(TARGET)] Done."

