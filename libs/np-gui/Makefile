TARGET = np-gui

CXX_FLAGS = $(CXX_GLOBAL_FLAGS) -Iinclude -I$(LIBS_DIR)/np-syscall/include -I$(LIBS_DIR)/np-syslib/include \
	-I$(LIBS_DIR)/np-userland/include \
	-fno-omit-frame-pointer -fno-stack-protector -ffreestanding -fno-rtti -fno-exceptions -Wall -Wextra \
	-fpie -std=c++17 -mno-80387 -mno-sse -mno-sse2 -mno-3dnow

CXX_SRCS = WindowServerClient.cpp Window.cpp

include $(LIB_COMMON_MK)

.PHONY: all

all: $(OBJS) 
	@echo "[$(TARGET)] Creating static library ..."
	@mkdir -p $(LIBS_OUTPUT_DIR)
	@$(AR) -rcs $(LIBS_OUTPUT_DIR)/lib$(TARGET).a $(OBJS)
	@echo "[$(TARGET)] Done."
