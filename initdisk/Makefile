FILES = dummy.txt
TAR_FLAGS = -c

ifneq ($(LOUD), @)
	TAR_FLAGS += -v
endif

.PHONY: all
all: $(INITDISK_FULL_FILEPATH)

$(INITDISK_FULL_FILEPATH): $(FILES)
	@echo "[Initdisk] Generating ..."
	$(LOUD)mkdir -p $(BUILD_DIR)
	$(LOUD)tar $(TAR_FLAGS) $(FILES) -f $(INITDISK_FULL_FILEPATH)
	@echo "[Initdisk] Done."

.PHONY: clean
clean:
	@echo "[Initdisk] Cleaning build files"
	$(LOUD)rm -r $(BUILD_DIR)
	@echo "[Initdisk] Done."
