BUILD_DIR := build
JOBS := $(shell nproc)
PREFIX := $(HOME)/.local

.PHONY: build install run test clean

build:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$(PREFIX)
	cmake --build $(BUILD_DIR) -j$(JOBS)

install: build
	cmake --install $(BUILD_DIR)

run: install
	$(PREFIX)/bin/suopNotes

test: build
	cd $(BUILD_DIR) && ctest --output-on-failure

clean:
	rm -rf $(BUILD_DIR)
