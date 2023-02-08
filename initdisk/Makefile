FILES = dummy.txt
TAR_FLAGS = -c

ifneq ($(LOUD), @)
	TAR_FLAGS += -v
endif

include $(PROJ_ROOT_DIR)/misc/Formatting.mk

.PHONY: all
all: $(INITDISK_FULL_FILEPATH)

$(INITDISK_FULL_FILEPATH): $(FILES)
	@echo -e "$(C_BLUE)[Initdisk]$(C_RST) Generating ..."
	$(LOUD)mkdir -p $(BUILD_DIR)
	$(LOUD)tar $(TAR_FLAGS) $(FILES) -f $(INITDISK_FULL_FILEPATH)
	@echo -e "$(C_BLUE)[Initdisk]$(C_RST) $(C_GREEN)Done.$(C_RST)"

.PHONY: clean
clean:
	@echo -e "$(C_BLUE)[Initdisk]$(C_RST) Cleaning build files ..."
	$(LOUD)rm -r $(BUILD_DIR)
	@echo -e "$(C_BLUE)[Initdisk]$(C_RST) $(C_GREEN)Done.$(C_RST)"
