BUILD := ../../build/s2render

$(BUILD)/s2render: main.cpp $(wildcard ../../EngineCore/*.hpp)
	@mkdir -p $(BUILD)
	clang++ -std=c++20 -O2 -Wall -Wextra -o $@ main.cpp

.PHONY: clean
clean:
	rm -rf $(BUILD)
