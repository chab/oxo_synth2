ARCH ?= $(shell uname -m)
BUILD := ../../build/regress

$(BUILD)/regress-$(ARCH): main.cpp $(wildcard ../../EngineCore/*.hpp)
	@mkdir -p $(BUILD)
	clang++ -std=c++20 -O2 -Wall -Wextra -arch $(ARCH) -o $@ main.cpp

.PHONY: clean
clean:
	rm -rf $(BUILD)
