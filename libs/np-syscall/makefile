TARGET = np-syscall

CXX_FLAGS = $(CXX_GLOBAL_FLAGS) -Iinclude -I$(LIBS_DIR)/np-syslib/include \
	-fno-omit-frame-pointer -fno-stack-protector -ffreestanding -fno-rtti -fno-exceptions -mno-red-zone -Wall -Wextra \
	-fpie -std=c++17 -mno-80387 -mno-sse -mno-sse2 -mno-3dnow

CXX_SRCS = SyscallFunctions.cpp

include $(LIB_COMMON_MK)
