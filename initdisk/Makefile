FILES = example.txt

all: $(FILES)
	@echo "[Initdisk] Generating init disk ..."
	@mkdir -p $(BUILD_DIR)
	@tar -vc $(FILES) -f $(INITDISK_FULL_FILEPATH)
	@echo "Done."

clean:
	@echo "[Initdisk] Cleaning build dir."
	@rm -r $(BUILD_DIR)
	@echo "Done."
