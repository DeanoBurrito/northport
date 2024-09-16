TARGET = Virtio

CXX_FLAGS = $(DRIVER_CXX_FLAGS) -Iinclude
LD_FLAGS = $(DRIVER_LD_FLAGS)

CXX_SRCS = Main.cpp Transport.cpp Gpu.cpp

include $(PROJ_ROOT_DIR)/misc/BuildCommon.mk

all: $(TARGET_DRIVER)
