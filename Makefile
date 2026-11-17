BOARD ?= g491re
BUILD_DIR := build/$(BOARD)
FW_DIR := $(BUILD_DIR)/src/app
ELF := $(FW_DIR)/firmware.elf
BIN := $(FW_DIR)/firmware.bin
HEX := $(FW_DIR)/firmware.hex

CMAKE ?= cmake
SIZE ?= arm-none-eabi-size

FLASH_TOOL ?= auto
FLASH_ADDR ?= 0x08000000
OPENOCD ?= openocd
OPENOCD_INTERFACE ?= interface/stlink.cfg
ifeq ($(BOARD),f446re)
OPENOCD_TARGET ?= target/stm32f4x.cfg
else
OPENOCD_TARGET ?= target/stm32g4x.cfg
endif

ifeq ($(BOARD),f446re)
CUBE_FW_PATH ?= $(firstword $(wildcard $(HOME)/STM32Cube/Repository/STM32Cube_FW_F4_*))
else
CUBE_FW_PATH ?= $(firstword $(wildcard $(HOME)/STM32Cube/Repository/STM32Cube_FW_G4_*))
endif
ifneq ($(CUBE_FW_PATH),)
CMAKE_ARGS += -DCUBE_FW_PATH=$(CUBE_FW_PATH)
endif

.DEFAULT_GOAL := all
.PHONY: all help config build size flash reset erase clean distclean

all: build

help:
	@echo "Usage: make [target] [BOARD=<board>] [FLASH_TOOL=auto|st-flash|openocd]"
	@echo ""
	@echo "  config     cmake --preset \$$(BOARD)"
	@echo "  build      configure and build (default)"
	@echo "  flash      build, then flash via st-flash or openocd"
	@echo "  reset      reset the board"
	@echo "  erase      mass erase"
	@echo "  size       show firmware size"
	@echo "  clean      clean the board build directory"
	@echo "  distclean  remove all build directories"
	@echo ""
	@echo "BOARD=$(BOARD)  BUILD_DIR=$(BUILD_DIR)"
	@echo "CUBE_FW_PATH=$(CUBE_FW_PATH)"

config:
	$(CMAKE) --preset $(BOARD) $(CMAKE_ARGS)

build: config
	$(CMAKE) --build $(BUILD_DIR)

size: build
	$(SIZE) $(ELF)

flash: build
ifeq ($(FLASH_TOOL),st-flash)
	st-flash --reset write $(BIN) $(FLASH_ADDR)
else ifeq ($(FLASH_TOOL),openocd)
	$(OPENOCD) -f $(OPENOCD_INTERFACE) -f $(OPENOCD_TARGET) \
		-c "program $(ELF) verify reset exit"
else
	@if command -v st-flash >/dev/null 2>&1; then \
		echo "Flashing $(BIN) with st-flash"; \
		st-flash --reset write $(BIN) $(FLASH_ADDR); \
	elif command -v $(OPENOCD) >/dev/null 2>&1; then \
		echo "Flashing $(ELF) with openocd"; \
		$(OPENOCD) -f $(OPENOCD_INTERFACE) -f $(OPENOCD_TARGET) \
			-c "program $(ELF) verify reset exit"; \
	else \
		echo "error: no flash tool found (install st-flash or openocd)"; \
		exit 1; \
	fi
endif

reset:
	@if command -v st-flash >/dev/null 2>&1; then \
		st-flash reset; \
	elif command -v $(OPENOCD) >/dev/null 2>&1; then \
		$(OPENOCD) -f $(OPENOCD_INTERFACE) -f $(OPENOCD_TARGET) \
			-c "init; reset run; exit"; \
	else \
		echo "error: no flash tool found"; \
		exit 1; \
	fi

erase:
	@if command -v st-flash >/dev/null 2>&1; then \
		st-flash erase; \
	elif command -v $(OPENOCD) >/dev/null 2>&1; then \
		$(OPENOCD) -f $(OPENOCD_INTERFACE) -f $(OPENOCD_TARGET) \
			-c "init; reset halt; flash erase_sector 0 0 last; reset run; exit"; \
	else \
		echo "error: no flash tool found"; \
		exit 1; \
	fi

clean:
	@if [ -d "$(BUILD_DIR)" ]; then $(CMAKE) --build $(BUILD_DIR) --target clean; fi

distclean:
	rm -rf build
