BUILD   := build

.PHONY: all build run clean

all: build

build:
	@cmake -S . -B $(BUILD)
	@cmake --build $(BUILD) -j12

run: build
	@./$(BUILD)/sequential
	@./$(BUILD)/coro
	@./$(BUILD)/async

clean:
	@rm -rf $(BUILD)

