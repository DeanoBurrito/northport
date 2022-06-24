TARGET = np-syslib

CXX_FLAGS = $(CXX_GLOBAL_FLAGS) -Iinclude \
	-fno-omit-frame-pointer -fno-stack-protector -ffreestanding -fno-rtti -fno-exceptions -mno-red-zone -Wall -fpie -std=c++17 \
	-mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-3dnow

CXX_SRCS = Memory.cpp String.cpp StringCulture.cpp FormatPrinter.cpp Format.cpp Bitmap.cpp \
	cultures/AustralianCulture.cpp \
	elf/HeaderParser.cpp elf/Demangle.cpp \
	 formats/Tar.cpp

include $(LIB_COMMON_MK)

.PHONY: all

all: $(OBJS)
	@echo "[$(TARGET)] Creating static library ..."
	@mkdir -p $(LIBS_OUTPUT_DIR)
	@$(AR) -rcs $(LIBS_OUTPUT_DIR)/lib$(TARGET).a $(OBJS)
	@echo "[$(TARGET)] Done."

