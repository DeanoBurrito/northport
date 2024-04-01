FILES = dummy.txt LICENSE
TAR_FLAGS = -c

ifneq ($(LOUD), @)
	TAR_FLAGS += -v
endif

include $(PROJ_ROOT_DIR)/misc/Formatting.mk

.PHONY: all
all: $(INITDISK_FULL_FILEPATH)

$(INITDISK_FULL_FILEPATH): $(FILES) $(DRIVERS_OUTPUT_DIR)/*
	@printf "$(C_BLUE)[Initdisk]$(C_RST) Generating ...\r\n"
	$(LOUD)mkdir -p $(BUILD_DIR)
	$(LOUD)mkdir -p drivers
	$(LOUD)cp -a $(DRIVERS_OUTPUT_DIR)/* drivers
	$(LOUD)tar $(TAR_FLAGS) $(FILES) drivers -f $(INITDISK_FULL_FILEPATH)
	@printf "$(C_BLUE)[Initdisk]$(C_RST) $(C_GREEN)Done.$(C_RST)\r\n"

LICENSE: $(PROJ_ROOT_DIR)/LICENSE
	$(LOUD)cp $(PROJ_ROOT_DIR)/LICENSE LICENSE

.PHONY: clean
clean:
	@printf "$(C_BLUE)[Initdisk]$(C_RST) Cleaning build files ...\r\n"
	$(LOUD)-rm -r $(BUILD_DIR)
	$(LOUD)-rm -r drivers
	@printf "$(C_BLUE)[Initdisk]$(C_RST) $(C_GREEN)Done.$(C_RST)\r\n"
