TARGET = server-window

CXX_FLAGS = -Iinclude -I$(LIBS_DIR)/np-syscall/include -I$(LIBS_DIR)/np-syslib/include \
	-I$(LIBS_DIR)/np-graphics/include -I$(LIBS_DIR)/np-userland/include  -I$(LIBS_DIR)/np-gui/include \
	-Wall -Wextra -fno-stack-protector -fno-omit-frame-pointer -ffreestanding \
	-mno-red-zone -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-3dnow \
	-std=c++17 -fno-rtti -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables

LD_LIBS = -L$(LIBS_OUTPUT_DIR) -lnp-graphics -lnp-syslib -lnp-syscall -lnp-userland 
LD_FLAGS = $(LD_LIBS) -nostdlib -zmax-page-size=0x1000 --no-dynamic-linker

CXX_SRCS = Main.cpp Renderer.cpp WindowManager.cpp \
	protocols/GenericProtocol.cpp protocols/IpcProtocol.cpp

include $(USERLAND_COMMON_MK)

.PHONY: all

all: $(OBJS)
	@echo "[$(TARGET)] Linking ..."
	@mkdir -p $(USERLAND_OUTPUT_DIR)
	@$(LD) $(OBJS) $(LD_FLAGS) -o $(USERLAND_OUTPUT_DIR)/$(TARGET).elf
	@echo "Done."
