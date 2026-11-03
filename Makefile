CXX ?= c++
CPPFLAGS += -Iinclude
CXXFLAGS ?= -O2 -Wall -Wextra -Wpedantic
BUILD_DIR ?= build
PREFIX ?= /usr/local

.PHONY: all test check install clean
all: $(BUILD_DIR)/suka $(BUILD_DIR)/libsuka.so

$(BUILD_DIR):
	mkdir -p "$@"

$(BUILD_DIR)/suka.o: src/suka.cpp include/suka.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -std=c++17 -fPIC -DSUKA_BUILDING_LIBRARY -MMD -MP -c $< -o $@

$(BUILD_DIR)/main.o: main.cpp include/suka.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -std=c++17 -MMD -MP -c $< -o $@

$(BUILD_DIR)/libsuka.so: $(BUILD_DIR)/suka.o
	$(CXX) $(LDFLAGS) -shared -Wl,-soname,libsuka.so -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/suka: $(BUILD_DIR)/main.o $(BUILD_DIR)/libsuka.so
	$(CXX) $(LDFLAGS) -o $@ $(BUILD_DIR)/main.o -L$(BUILD_DIR) -lsuka -Wl,-rpath,'$$ORIGIN:$$ORIGIN/../lib' $(LDLIBS)

test: all
	python3 -m unittest discover -s tests -v

check: all
	$(BUILD_DIR)/suka --check .suka/main.suka

install: all
	install -d "$(DESTDIR)$(PREFIX)/bin" "$(DESTDIR)$(PREFIX)/lib" "$(DESTDIR)$(PREFIX)/include"
	install -m 755 $(BUILD_DIR)/suka "$(DESTDIR)$(PREFIX)/bin/suka"
	install -m 755 $(BUILD_DIR)/libsuka.so "$(DESTDIR)$(PREFIX)/lib/libsuka.so"
	install -m 644 include/suka.h "$(DESTDIR)$(PREFIX)/include/suka.h"

clean:
	rm -f $(BUILD_DIR)/suka $(BUILD_DIR)/libsuka.so $(BUILD_DIR)/suka.o $(BUILD_DIR)/main.o $(BUILD_DIR)/*.d

-include $(BUILD_DIR)/suka.d $(BUILD_DIR)/main.d
