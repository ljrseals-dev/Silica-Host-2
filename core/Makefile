# Native build/test of the Silica AArch64 core (the wasm build is core/wasm/build-wasm.sh).
CXX  ?= g++
STD  := -std=c++20
WARN := -Wall -Wextra -Wpedantic -Werror
INC  := -Iinclude
REL  := -O3 -DNDEBUG -flto -fno-exceptions -fno-rtti
BUILD := build
HDRS := $(wildcard include/silica/*.hpp)

all: $(BUILD)/silica-sim $(BUILD)/tests $(BUILD)/tests-ext
$(BUILD):
	mkdir -p $(BUILD)
$(BUILD)/tests: tests/test_main.cpp $(HDRS) | $(BUILD)
	$(CXX) $(STD) $(WARN) $(INC) $(REL) $< -o $@
$(BUILD)/tests-ext: tests/test_ext.cpp $(HDRS) | $(BUILD)
	$(CXX) $(STD) $(WARN) $(INC) $(REL) $< -o $@
$(BUILD)/silica-sim: src/main.cpp $(HDRS) | $(BUILD)
	$(CXX) $(STD) $(WARN) $(INC) $(REL) $< -o $@
$(BUILD)/bench: benches/bench_main.cpp $(HDRS) | $(BUILD)
	$(CXX) $(STD) $(WARN) $(INC) $(REL) $< -o $@
$(BUILD)/tests-san: tests/test_main.cpp $(HDRS) | $(BUILD)
	$(CXX) $(STD) $(WARN) $(INC) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all $< -o $@
$(BUILD)/tests-ext-san: tests/test_ext.cpp $(HDRS) | $(BUILD)
	$(CXX) $(STD) $(WARN) $(INC) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all $< -o $@

test: $(BUILD)/tests $(BUILD)/tests-ext
	$(BUILD)/tests && $(BUILD)/tests-ext
san: $(BUILD)/tests-san $(BUILD)/tests-ext-san
	$(BUILD)/tests-san && $(BUILD)/tests-ext-san
bench: $(BUILD)/bench
	$(BUILD)/bench
clean:
	rm -rf $(BUILD)
.PHONY: all test san bench clean
