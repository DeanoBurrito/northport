TARGET = Test

CXX_FLAGS = -Wall -Wextra -std=c++17 -fno-rtti -fno-exceptions -fno-unwind-tables \
	-fno-asynchronous-unwind-tables -Iinclude -ffreestanding
LD_FLAGS = -nostdlib -static -r

CXX_SRCS = Main.cpp

include $(PROJ_ROOT_DIR)/misc/BuildCommon.mk

all: $(TARGET_DRIVER)

