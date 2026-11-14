BUILD := ../../build/s1ref

$(BUILD)/s1ref: s1ref.c
	@mkdir -p $(BUILD)
	clang -arch x86_64 -O2 -Wall -Wextra -o $@ $< -framework AudioToolbox -framework CoreFoundation

.PHONY: clean
clean:
	rm -rf $(BUILD)
