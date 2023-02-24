FILES = dummy.txt
TAR_FLAGS = -c

ifneq ($(LOUD), @)
	TAR_FLAGS += -v
endif

include $(PROJ_ROOT_DIR)/misc/Formatting.mk

.PHONY: all
all: $(INITDISK_FULL_FILEPATH)

$(INITDISK_FULL_FILEPATH): $(FILES)
	@printf "$(C_BLUE)[Initdisk]$(C_RST) Generating ...\r\n"
	$(LOUD)mkdir -p $(BUILD_DIR)
	$(LOUD)tar $(TAR_FLAGS) $(FILES) -f $(INITDISK_FULL_FILEPATH)
	@printf "$(C_BLUE)[Initdisk]$(C_RST) $(C_GREEN)Done.$(C_RST)\r\n"

.PHONY: clean
clean:
	@printf "$(C_BLUE)[Initdisk]$(C_RST) Cleaning build files ...\r\n"
	$(LOUD)rm -r $(BUILD_DIR)
	@printf "$(C_BLUE)[Initdisk]$(C_RST) $(C_GREEN)Done.$(C_RST)\r\n"
