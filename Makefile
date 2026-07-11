.PHONY: setup config compile test smoke check clean

PYTHON ?= python
ESPHOME ?= esphome
CXX ?= c++
CONFIG := wallbox-powerboost-emulator.yaml
TEST_BIN := /tmp/wallbox-powerboost-emulator-tests
PLATFORMIO_CORE_DIR ?= $(CURDIR)/.platformio
PORT ?= /dev/ttyUSB0
SLAVE ?= 1
BAUD ?= 9600
PARITY ?= N
SMOKE_ARGS ?=

export PLATFORMIO_CORE_DIR

setup:
	$(PYTHON) -m pip install -r requirements.txt

config:
	$(ESPHOME) config $(CONFIG)

compile:
	$(ESPHOME) compile $(CONFIG)

test:
	$(CXX) -std=c++17 -Wall -Wextra -pedantic -Icomponents/em112_bridge \
		components/em112_bridge/dsmr_client.cpp \
		components/em112_bridge/em112_registers.cpp \
		components/em112_bridge/modbus_rtu_slave.cpp \
		components/em112_bridge/debug_ring.cpp \
		tests/test_register_packing.cpp \
		-o $(TEST_BIN)
	$(TEST_BIN)

smoke:
	$(PYTHON) tools/modbus_smoke_test.py --port $(PORT) --slave $(SLAVE) --baud $(BAUD) --parity $(PARITY) $(SMOKE_ARGS)

check: test config compile

clean:
	rm -f $(TEST_BIN)
