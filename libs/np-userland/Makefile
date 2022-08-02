TARGET = np-userland

CXX_FLAGS = $(CXX_GLOBAL_FLAGS) -Iinclude -I$(LIBS_DIR)/np-syscall/include -I$(LIBS_DIR)/np-syslib/include \
	-fno-omit-frame-pointer -fno-stack-protector -ffreestanding -fno-rtti -fno-exceptions -mno-red-zone -Wall -Wextra \
	-fpie -std=c++17 -mno-80387 -mno-sse -mno-sse2 -mno-3dnow

CXX_SRCS = New.cpp Userland.cpp Logging.cpp \
	heap/UserHeap.cpp heap/UserSlab.cpp heap/UserPool.cpp 

include $(LIB_COMMON_MK)
