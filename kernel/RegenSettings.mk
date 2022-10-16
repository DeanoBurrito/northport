SETTINGS_SRC = $(BUILD_DIR)/config/RegenSettings.cpp
CXX_SRCS += $(SETTINGS_SRC)

# Yes, I know this is cursed and this is probably a job for autotools.
.PHONY: $(SETTINGS_SRC)
$(SETTINGS_SRC):
	@mkdir -p $(@D)
	@echo "[Kernel] Regenerating settings file ..."
	@echo "#include <config/Settings.h> \n" > $(SETTINGS_SRC)
	@echo "namespace Npk::Config \n{ \nvoid AddRegeneratedSettings() \n{" >> $(SETTINGS_SRC)
	@echo "} \n}" >> $(SETTINGS_SRC)
