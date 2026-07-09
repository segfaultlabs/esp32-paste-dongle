CXX ?= g++
CXXFLAGS = -std=c++17 -Wall -Wextra -I.

SRC_DIR = ../src

TESTS = test_macro_parser test_keymap test_typing_engine test_paster

.PHONY: all clean run

all: $(TESTS)

test_macro_parser: test_macro_parser.cpp $(SRC_DIR)/macro_parser.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_keymap: test_keymap.cpp $(SRC_DIR)/keymap.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_typing_engine: test_typing_engine.cpp $(SRC_DIR)/typing_engine.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_paster: test_paster.cpp $(SRC_DIR)/paster.cpp $(SRC_DIR)/typing_engine.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

run: all
	@for t in $(TESTS); do \
		echo "=== Running $$t ==="; \
		./$$t || exit 1; \
	done
	@echo "All tests passed."

clean:
	rm -f $(TESTS)
