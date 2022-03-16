FILES = example.txt apps/ config/ 

.PHONY: clean

all: $(FILES)
	@echo "[Initdisk] Generating init disk ..."
	@mkdir -p $(BUILD_DIR)
	@cp -v -r $(USERLAND_OUTPUT_DIR)/. apps/
	@tar -vc $(FILES) -f $(INITDISK_FULL_FILEPATH)
	@echo "Done."

clean:
	@echo "[Initdisk] Cleaning build dir."
	@rm -r $(BUILD_DIR)
	@rm -r apps
	@echo "[Initdisk] Done."

#generate this folder as we populate it from userland/build
apps/:
	@mkdir -p apps
