BUILD_DIR = build

.PHONY: all
all:
	@echo "[Docs] Creating pdf ..."
	@mkdir -p $(BUILD_DIR)
	@-rm $(BUILD_DIR)/*.pdf
	@pdflatex -output-directory $(BUILD_DIR) Manual.tex
	@pdflatex -output-directory $(BUILD_DIR) Manual.tex
	@mv $(BUILD_DIR)/Manual.pdf $(BUILD_DIR)/northport-manual-$(shell date +%F).pdf
	@echo "[Docs] Done, pdf available at: $(abspath $(BUILD_DIR))"

.PHONY: clean
clean:
	@rm -r $(BUILD_DIR)
