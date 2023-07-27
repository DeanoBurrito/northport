DEVEL_CACHE_DIR = $(abspath .devel-cache)

export OVMF_FILE = $(DEVEL_CACHE_DIR)/ovmf/$(CPU_ARCH).fd
export LIMINE_DIR = $(DEVEL_CACHE_DIR)/limine

ifeq ($(CPU_ARCH), x86_64)
	OVMF_DOWNLOAD_URL = https://retrage.github.io/edk2-nightly/bin/RELEASEX64_OVMF.fd
else ifeq ($(CPU_ARCH), riscv64)
	OVMF_DOWNLOAD_URL = https://retrage.github.io/edk2-nightly/bin/RELEASERISCV64_VIRT.fd
	OVMF_POST_CMD = && dd if=/dev/zero of=$(OVMF_FILE) bs=1 count=0 seek=33554432
endif

$(DEVEL_CACHE_DIR):
	$(LOUD)mkdir -p $(DEVEL_CACHE_DIR)/exists

$(OVMF_FILE): $(DEVEL_CACHE_DIR)/exists/ovmf-$(CPU_ARCH)
$(LIMINE_DIR): $(DEVEL_CACHE_DIR)/exists/limine

$(DEVEL_CACHE_DIR)/exists/ovmf-$(CPU_ARCH): $(DEVEL_CACHE_DIR)
	$(LOUD)-rm -rf $(OVMF_FILE)
	$(LOUD)mkdir -p $(DEVEL_CACHE_DIR)/ovmf
	$(LOUD)curl -o $(OVMF_FILE) $(OVMF_DOWNLOAD_URL) $(OVMF_POST_CMD)
	$(LOUD)touch $@
	@printf "$(C_YELLOW)[Cache]$(C_RST) Downloaded OVMF firmware for $(CPU_ARCH), from $(OVMF_DOWNLOAD_URL).\r\n"

$(DEVEL_CACHE_DIR)/exists/limine: $(DEVEL_CACHE_DIR)
	$(LOUD)-rm -rf $(LIMINE_DIR)
	$(LOUD)git clone https://github.com/limine-bootloader/limine.git \
		--branch=v5.x-branch-binary --depth 1 $(LIMINE_DIR)
	$(LOUD)cd $(LIMINE_DIR); make all
	$(LOUD)touch $@
	@printf "$(C_YELLOW)[Cache]$(C_RST) Limine repo cloned from latest v5 release.\r\n"

.PHONY: cache-clean
cache-clean: $(DEVEL_CACHE_DIR)
	$(LOUD)rm -rf $(DEVEL_CACHE_DIR)
	@printf "$(C_YELLOW)[Cache]$(C_RST) Devel cache cleaned, files will be downloaded again as needed.\r\n"

