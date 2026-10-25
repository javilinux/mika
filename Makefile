PORT ?= $(shell ls /dev/cu.usbmodem* 2>/dev/null | head -n 1)
PIO = .venv/bin/pio
PYTHON = .venv/bin/python3
ESPTOOL = /Users/jramirez/.platformio/packages/tool-esptool/esptool
BACKUP_DIR = backups/mika-gsec-working

.PHONY: all build flash fs monitor clean snapshot restore backup-full restore-full convert-sfx

all: build flash

build:
	$(PIO) run

flash:
	$(PIO) run -t upload --upload-port $(PORT)

fs:
	$(PYTHON) tools/prepare_device_fs.py
	$(PIO) run -t uploadfs --upload-port $(PORT)

monitor:
	$(PIO) device monitor -p $(PORT) -b 115200

clean:
	$(PIO) run -t clean

snapshot: build
	$(PIO) run -t buildfs
	mkdir -p $(BACKUP_DIR)
	cp .pio/build/m5stack-cores3/bootloader.bin $(BACKUP_DIR)/
	cp .pio/build/m5stack-cores3/partitions.bin $(BACKUP_DIR)/
	cp .pio/build/m5stack-cores3/firmware.bin $(BACKUP_DIR)/
	cp .pio/build/m5stack-cores3/littlefs.bin $(BACKUP_DIR)/
	@echo "Snapshot saved in $(BACKUP_DIR)"

restore:
	$(ESPTOOL) --chip esp32s3 --port $(PORT) --baud 921600 write-flash \
		0x0000 $(BACKUP_DIR)/bootloader.bin \
		0x8000 $(BACKUP_DIR)/partitions.bin \
		0x10000 $(BACKUP_DIR)/firmware.bin \
		0xc90000 $(BACKUP_DIR)/littlefs.bin

backup-full:
	mkdir -p backups
	$(ESPTOOL) --chip esp32s3 --port $(PORT) --baud 921600 read-flash 0x0 0x1000000 backups/mika_full_16MB.bin

restore-full:
	$(ESPTOOL) --chip esp32s3 --port $(PORT) --baud 921600 write-flash 0x0 backups/mika_full_16MB.bin
