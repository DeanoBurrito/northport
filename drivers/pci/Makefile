TARGET = Pci

CXX_FLAGS = $(DRIVER_CXX_FLAGS) -Iinclude
LD_FLAGS = $(DRIVER_LD_FLAGS)

CXX_SRCS = Main.cpp PciSegment.cpp NameLookup.cpp

include $(PROJ_ROOT_DIR)/misc/BuildCommon.mk

all: $(TARGET_DRIVER)
