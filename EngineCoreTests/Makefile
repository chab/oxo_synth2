CXX ?= clang++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Werror -O1 -g

BUILD_DIR := build
RUNNER := $(BUILD_DIR)/engine_core_tests
SRCS := $(wildcard *.cpp)
LIBS := -lz
ifeq ($(shell uname),Darwin)
LIBS += -framework CoreFoundation
endif
HDRS := $(wildcard *.hpp) $(wildcard ../EngineCore/*.hpp) ../EditorCore/Json.hpp ../EditorCore/Settings.hpp ../EditorCore/Themes.hpp ../EditorCore/UpdateCheck.hpp \
        ../Windows_Build/Source/WindowsZoomSettings.hpp \
        ../Shared/SynthTheme.swift ../Shared/SynthUpdateCheck.swift

.PHONY: test clean

test: $(RUNNER)
	./$(RUNNER)

$(RUNNER): $(SRCS) $(HDRS) legacy_fixture.txt
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I.. -DOXO_TESTS_DIR='"$(CURDIR)"' $(SRCS) -o $@ $(LIBS)

clean:
	rm -rf $(BUILD_DIR)
