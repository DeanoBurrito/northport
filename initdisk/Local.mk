INITDISK_TARGET = $(BUILD_DIR)/npk-initdisk.tar
INITDISK_FILES = dummy.txt

PCI_IDS_FILE = $(VENDOR_CACHE_DIR)/pci.ids
PCI_IDS_URL = https://pci-ids.ucw.cz/v2.2/pci.ids
INITDISK_CACHE_FILES = $(PCI_IDS_FILE)

TAR_FLAGS = -c
ifneq ($(LOUD), @)
	TAR_FLAGS += -v
endif

ENABLED_TARGETS += $(INITDISK_TARGET)

$(INITDISK_TARGET): $(addprefix initdisk/, $(INITDISK_FILES)) $(INITDISK_CACHE_FILES) LICENSE
	@printf "$(C_BLUE)[Initdisk]$(C_RST) Generating ...\n"
	$(LOUD)tar $(TAR_FLAGS) $(addprefix initdisk/, $(INITDISK_FILES)) LICENSE -f $(INITDISK_TARGET)
	@printf "$(C_BLUE)[Initdisk]$(C_RST) $(C_GREEN)Done.$(C_RST)\n"

$(PCI_IDS_FILE):
	$(LOUD)-rm $(PCI_IDS_FILE)
	$(LOUD)mkdir -p $(VENDOR_CACHE_DIR)
	$(LOUD)curl -o $(PCI_IDS_FILE) $(PCI_IDS_URL)
	@printf "$(C_YELLOW)[Cache]$(C_RST)Downloaded pci.ids list from $(PCI_IDS_URL).\n"
