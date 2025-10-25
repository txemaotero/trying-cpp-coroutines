PROJECT := trying_coroutines
BUILD   := build

.PHONY: all build run clean

all: build

build:
	@cmake -S . -B $(BUILD)
	@cmake --build $(BUILD) -j

run: build
	@./$(BUILD)/$(PROJECT)

clean:
	@rm -rf $(BUILD)

