CXX ?= g++
CXXFLAGS = -std=c++17 -Wall -Wextra -I.

SRC_DIR = ../src

TESTS = test_macro_parser test_keymap test_typing_engine test_paster test_macro_runner test_human_sim test_transliterate

.PHONY: all clean run

all: $(TESTS)

test_macro_parser: test_macro_parser.cpp $(SRC_DIR)/macro_parser.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_keymap: test_keymap.cpp $(SRC_DIR)/keymap.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_typing_engine: test_typing_engine.cpp $(SRC_DIR)/typing_engine.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_paster: test_paster.cpp $(SRC_DIR)/paster.cpp $(SRC_DIR)/typing_engine.cpp $(SRC_DIR)/transliterate.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_macro_runner: test_macro_runner.cpp $(SRC_DIR)/macro_runner.cpp $(SRC_DIR)/macro_parser.cpp $(SRC_DIR)/keymap.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_human_sim: test_human_sim.cpp $(SRC_DIR)/human_sim.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_transliterate: test_transliterate.cpp $(SRC_DIR)/transliterate.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

run: all
	@for t in $(TESTS); do \
		echo "=== Running $$t ==="; \
		./$$t || exit 1; \
	done
	@echo "All tests passed."

clean:
	rm -f $(TESTS)
