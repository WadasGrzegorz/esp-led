.PHONY: compile prepare ota-image upload monitor run test

test:
	mkdir -p .pio/native-test
	$(CXX) -std=c++17 -Wall -Wextra -Werror -Itest/native/stubs -Ifirmware \
		test/native/start_delay.cpp firmware/src/config/LightingConfig.cpp \
		firmware/src/config/ConfigurationManager.cpp \
		firmware/src/led/LedController.cpp firmware/src/led/AnimationEngine.cpp \
		-o .pio/native-test/start-delay
	.pio/native-test/start-delay
	$(CXX) -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
		-fno-omit-frame-pointer -Itest/native/stubs -Ifirmware \
		test/native/led_capacity.cpp firmware/src/config/LightingConfig.cpp \
		firmware/src/config/ConfigurationManager.cpp \
		firmware/src/led/LedController.cpp firmware/src/led/AnimationEngine.cpp \
		firmware/src/setup/SetupController.cpp -o .pio/native-test/led-capacity
	.pio/native-test/led-capacity
	node --test test/web_ui.test.cjs

compile:
	pio run

prepare: compile
	cp .pio/build/esp32-c6/firmware.bin firmware.bin
	@echo "Prepared firmware: $(CURDIR)/firmware.bin"

ota-image: compile
	@echo "OTA image: $(CURDIR)/.pio/build/esp32-c6/firmware.bin"

upload:
	pio run -t upload

monitor:
	pio device monitor

run: upload
	pio device monitor
