# Main (working) build options:
#  native (default) - Optimized version of code.
#  debug - turn on all debugging options, including asserts and pointer tracking.
#  quick - no debugging or optimization; fastest compile time.
#  clean - remove all compilation artifacts including executable.
#  web - compile to WebAssembly with optimizations on (same as web-native)
#  web-debug - compile to WebAssembly with debug on (same as web-native)
#  web-quick - compile to WebAssembly with neither debug or optimization
#
#
# Other build options, more rarely used:
#  grumpy - Lots of extra warnings turned on

AVIDA_DIR := ..
default: native

include $(AVIDA_DIR)/Makefile-base.mk

TARGET := Avida
CLEAN_EXTRA = $(TARGET)

native: FLAGS := $(FLAGS_OPT)
native: $(TARGET)

debug: FLAGS := $(FLAGS_DEBUG)
debug: $(TARGET)
debug: $(TARGET)

grumpy: FLAGS := $(FLAGS_GRUMPY)
grumpy: $(TARGET)

quick: FLAGS := $(FLAGS_QUICK)
quick: $(TARGET)

$(TARGET): $(TARGET).cpp
	$(CXX) $(FLAGS) $(TARGET).cpp -o $(TARGET)

new: clean
new: native
