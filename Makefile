.PHONY: compile upload monitor run

compile:
	pio run

upload:
	pio run -t upload

monitor:
	pio device monitor

run: upload
	pio device monitor
