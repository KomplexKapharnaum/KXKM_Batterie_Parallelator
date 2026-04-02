# Host-compiled Unity tests for ESP-IDF firmware
# Usage: make all

CXX      ?= g++
CXXFLAGS  = -Wall -Wextra -DNATIVE_TEST -std=c++17
UNITY_DIR = unity
UNITY_SRC = $(UNITY_DIR)/unity.c
UNITY_INC = -I$(UNITY_DIR)

TESTS = test_protection test_vrm_topics test_ble_victron test_config_labels test_vedirect_parser

.PHONY: all clean download_unity run

all: download_unity $(TESTS) run

download_unity:
	@if [ ! -f $(UNITY_DIR)/unity.h ]; then \
		echo "Downloading Unity..."; \
		mkdir -p $(UNITY_DIR); \
		curl -sL https://raw.githubusercontent.com/ThrowTheSwitch/Unity/v2.6.1/src/unity.h -o $(UNITY_DIR)/unity.h; \
		curl -sL https://raw.githubusercontent.com/ThrowTheSwitch/Unity/v2.6.1/src/unity.c -o $(UNITY_DIR)/unity.c; \
		curl -sL https://raw.githubusercontent.com/ThrowTheSwitch/Unity/v2.6.1/src/unity_internals.h -o $(UNITY_DIR)/unity_internals.h; \
	fi

test_protection: download_unity
	$(CXX) $(CXXFLAGS) $(UNITY_INC) -o $@ test_protection/main/test_protection.cpp $(UNITY_SRC)

test_vrm_topics: download_unity
	$(CXX) $(CXXFLAGS) $(UNITY_INC) -o $@ test_vrm_topics/main/test_vrm_topics.cpp $(UNITY_SRC)

test_ble_victron: download_unity
	$(CXX) $(CXXFLAGS) $(UNITY_INC) -o $@ test_ble_victron/main/test_ble_victron.cpp $(UNITY_SRC)

test_config_labels: download_unity
	$(CXX) $(CXXFLAGS) $(UNITY_INC) -o $@ test_config_labels/main/test_config_labels.cpp $(UNITY_SRC)

test_vedirect_parser: download_unity
	$(CXX) $(CXXFLAGS) $(UNITY_INC) -o $@ test_vedirect_parser/main/test_vedirect_parser.cpp $(UNITY_SRC)

run: $(TESTS)
	@echo "=== Running all host tests ==="
	@failed=0; \
	for t in $(TESTS); do \
		echo "--- $$t ---"; \
		./$$t || failed=1; \
	done; \
	if [ $$failed -ne 0 ]; then echo "SOME TESTS FAILED"; exit 1; fi
	@echo "=== All tests passed ==="

clean:
	rm -f $(TESTS)
	rm -rf $(UNITY_DIR)
