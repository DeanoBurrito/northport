FILES = example.txt $(shell find apps)

.PHONY: clean

all: $(FILES)
	@echo "[Initdisk] Generating init disk ..."
	@mkdir -p apps
	@mkdir -p $(BUILD_DIR)
	@cp -v -r $(USERLAND_OUTPUT_DIR)/. apps/
	@tar -vc $(FILES) -f $(INITDISK_FULL_FILEPATH)
	@echo "Done."

clean:
	@echo "[Initdisk] Cleaning build dir."
	@rm -r $(BUILD_DIR)
	@echo "[Initdisk] Done."
