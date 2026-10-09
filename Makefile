.PHONY: compile ota-image upload monitor run

compile:
	pio run

ota-image: compile
	@echo "OTA image: $(CURDIR)/.pio/build/esp32-c6/firmware.bin"

upload:
	pio run -t upload

monitor:
	pio device monitor

run: upload
	pio device monitor
