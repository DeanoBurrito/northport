INITDISK_TARGET = $(BUILD_DIR)/npk-initdisk.tar
INITDISK_FILES = dummy.txt pci.ids

TAR_FLAGS = -c

ifneq ($(LOUD), @)
	TAR_FLAGS += -v
endif

ENABLED_TARGETS += $(INITDISK_TARGET)

$(INITDISK_TARGET): $(addprefix initdisk/, $(INITDISK_FILES)) LICENSE
	@printf "$(C_BLUE)[Initdisk]$(C_RST) Generating ...\n"
	$(LOUD)tar $(TAR_FLAGS) $(addprefix initdisk/, $(INITDISK_FILES)) LICENSE -f $(INITDISK_TARGET)
	@printf "$(C_BLUE)[Initdisk]$(C_RST) $(C_GREEN)Done.$(C_RST)\n"
