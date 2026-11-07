.PHONY: compile prepare ota-image upload monitor run test

test:
	mkdir -p .pio/native-test
	$(CXX) -std=c++17 -Wall -Wextra -Werror -Itest/native/stubs -Ifirmware \
		test/native/start_delay.cpp firmware/src/config/LightingConfig.cpp \
		firmware/src/config/ConfigurationManager.cpp \
		firmware/src/diagnostics/Diagnostics.cpp \
		firmware/src/led/LedController.cpp firmware/src/led/AnimationEngine.cpp \
		-o .pio/native-test/start-delay
	.pio/native-test/start-delay
	$(CXX) -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
		-fno-omit-frame-pointer -Itest/native/stubs -Ifirmware \
		test/native/led_capacity.cpp firmware/src/config/LightingConfig.cpp \
		firmware/src/config/ConfigurationManager.cpp \
		firmware/src/diagnostics/Diagnostics.cpp \
		firmware/src/led/LedController.cpp firmware/src/led/AnimationEngine.cpp \
		firmware/src/setup/SetupController.cpp -o .pio/native-test/led-capacity
	.pio/native-test/led-capacity
	$(CXX) -std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined \
		-fno-omit-frame-pointer -Itest/native/stubs -Ifirmware \
		test/native/fade_out.cpp firmware/src/config/LightingConfig.cpp \
		firmware/src/led/LedController.cpp firmware/src/led/AnimationEngine.cpp \
		firmware/src/sensors/PirSensor.cpp firmware/src/diagnostics/Diagnostics.cpp \
		-o .pio/native-test/fade-out
	.pio/native-test/fade-out
	$(CXX) -std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined \
		-fno-omit-frame-pointer -Itest/native/stubs -Ifirmware \
		test/native/strip_animation.cpp firmware/src/config/LightingConfig.cpp \
		firmware/src/diagnostics/Diagnostics.cpp firmware/src/led/LedController.cpp \
		firmware/src/led/AnimationEngine.cpp -o .pio/native-test/strip-animation
	.pio/native-test/strip-animation
	$(CXX) -std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined \
		-fno-omit-frame-pointer -Itest/native/stubs -Ifirmware \
		test/native/diagnostics.cpp firmware/src/diagnostics/Diagnostics.cpp \
		firmware/src/diagnostics/OutputDiagnostic.cpp \
		firmware/src/config/LightingConfig.cpp firmware/src/led/LedController.cpp \
		firmware/src/led/AnimationEngine.cpp -o .pio/native-test/diagnostics
	.pio/native-test/diagnostics
	$(CXX) -std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined \
		-fno-omit-frame-pointer -Itest/native/stubs -Ifirmware \
		-I.pio/libdeps/esp32-c6/ArduinoJson/src \
		test/native/rmt_telemetry.cpp firmware/src/diagnostics/Diagnostics.cpp \
		firmware/src/diagnostics/DiagnosticsJson.cpp firmware/src/diagnostics/RmtDiagnosticsHooks.cpp \
		firmware/src/config/LightingConfig.cpp -o .pio/native-test/rmt-telemetry
	.pio/native-test/rmt-telemetry
	python3 -m unittest discover -s test -p 'test_*output.py' -v
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
