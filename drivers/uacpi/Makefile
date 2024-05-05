TARGET = Uacpi
CLONE_URL = https://github.com/ultraos/uacpi
CLONE_DIR = .upstream

COMMON_FLAGS = -Iinclude -I$(CLONE_DIR)/include -DUACPI_SIZED_FREES -DUACPI_OVERRIDE_STDLIB
CXX_FLAGS = $(DRIVER_CXX_FLAGS) $(COMMON_FLAGS)
C_FLAGS = $(DRIVER_C_FLAGS) $(COMMON_FLAGS)
LD_FLAGS = $(DRIVER_LD_FLAGS)

CXX_SRCS = Main.cpp Stdlib.cpp BindsIo.cpp BindsSync.cpp BindsMisc.cpp
C_SRCS = $(wildcard $(CLONE_DIR)/source/*.c)

include $(PROJ_ROOT_DIR)/misc/BuildCommon.mk

all: $(TARGET_DRIVER)

$(CXX_SRCS): $(CLONE_DIR)

$(CLONE_DIR):
	$(LOUD)-rm -rf $(CLONE_DIR)
	$(LOUD)git clone $(CLONE_URL) $(CLONE_DIR)
