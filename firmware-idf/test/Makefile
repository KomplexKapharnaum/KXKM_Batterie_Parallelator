# Host-compiled Unity tests for ESP-IDF firmware
# Usage: make all

CXX      ?= g++
CXXFLAGS  = -Wall -Wextra -DNATIVE_TEST -std=c++17
UNITY_DIR = unity
UNITY_SRC = $(UNITY_DIR)/unity.c
UNITY_INC = -I$(UNITY_DIR)
BUILD     = build

TESTS = test_protection test_vrm_topics test_ble_victron test_config_labels test_vedirect_parser
BINS  = $(addprefix $(BUILD)/,$(TESTS))

.PHONY: all clean download_unity run

all: download_unity $(BINS) run

download_unity:
	@if [ ! -f $(UNITY_DIR)/unity.h ]; then \
		echo "Downloading Unity..."; \
		mkdir -p $(UNITY_DIR); \
		curl -sL https://raw.githubusercontent.com/ThrowTheSwitch/Unity/v2.6.1/src/unity.h -o $(UNITY_DIR)/unity.h; \
		curl -sL https://raw.githubusercontent.com/ThrowTheSwitch/Unity/v2.6.1/src/unity.c -o $(UNITY_DIR)/unity.c; \
		curl -sL https://raw.githubusercontent.com/ThrowTheSwitch/Unity/v2.6.1/src/unity_internals.h -o $(UNITY_DIR)/unity_internals.h; \
	fi

$(BUILD)/%: %/main/*.cpp download_unity
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(UNITY_INC) -o $@ $< $(UNITY_SRC)

run: $(BINS)
	@echo "=== Running all host tests ==="
	@failed=0; \
	for t in $(BINS); do \
		echo "--- $$t ---"; \
		./$$t || failed=1; \
	done; \
	if [ $$failed -ne 0 ]; then echo "SOME TESTS FAILED"; exit 1; fi
	@echo "=== All tests passed ==="

clean:
	rm -rf $(BUILD)
	rm -rf $(UNITY_DIR)
