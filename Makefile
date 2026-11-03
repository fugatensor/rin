CXX ?= c++
CPPFLAGS += -Iinclude
CXXFLAGS ?= -O2 -Wall -Wextra -Wpedantic
BUILD_DIR ?= build
PREFIX ?= /usr/local

.PHONY: all ffi test check install install-ffi clean
all: $(BUILD_DIR)/rin
ffi: $(BUILD_DIR)/librin.so

$(BUILD_DIR):
	mkdir -p "$@"

$(BUILD_DIR)/rin.o: src/rin.cpp include/rin.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -std=c++17 -fPIC -DRIN_BUILDING_LIBRARY -MMD -MP -c $< -o $@

$(BUILD_DIR)/main.o: main.cpp include/rin/pass.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -std=c++17 -MMD -MP -c $< -o $@

$(BUILD_DIR)/librin.so: $(BUILD_DIR)/rin.o
	$(CXX) $(LDFLAGS) -shared -Wl,-soname,librin.so -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/rin: $(BUILD_DIR)/main.o
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test: all ffi
	python3 -m unittest discover -s tests -v

check: all
	$(BUILD_DIR)/rin --check .rin/main.rin

install: all
	install -d "$(DESTDIR)$(PREFIX)/bin" "$(DESTDIR)$(PREFIX)/include/rin"
	install -m 755 $(BUILD_DIR)/rin "$(DESTDIR)$(PREFIX)/bin/rin"
	install -m 644 include/rin.h "$(DESTDIR)$(PREFIX)/include/rin.h"
	install -m 644 include/rin/pass.h "$(DESTDIR)$(PREFIX)/include/rin/pass.h"

install-ffi: ffi
	install -d "$(DESTDIR)$(PREFIX)/lib" "$(DESTDIR)$(PREFIX)/include"
	install -m 755 $(BUILD_DIR)/librin.so "$(DESTDIR)$(PREFIX)/lib/librin.so"
	install -m 644 include/rin.h "$(DESTDIR)$(PREFIX)/include/rin.h"

clean:
	rm -f $(BUILD_DIR)/rin $(BUILD_DIR)/librin.so $(BUILD_DIR)/rin.o $(BUILD_DIR)/main.o $(BUILD_DIR)/*.d

-include $(BUILD_DIR)/rin.d $(BUILD_DIR)/main.d
