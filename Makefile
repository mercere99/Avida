# This is the Makefile for Avida
# To use, you must:
#  - setup EMP_DIR to indicate the location of the Empirical root directory.
#
# Build options:
#  native (default) - Optimized version of code.
#  debug - turn on all debugging options, including asserts and pointer tracking.
#  quick - no debugging or optimization; fastest compile time.
#  clean - remove all compilation artifacts including executable.
#  web - compile to WebAssembly with optimizations on (same as web-native)
#  web-debug - compile to WebAssembly with debug on (same as web-native)
#  web-quick - compile to WebAssembly with neither debug or optimization
#
# Other build options, less used:
#  grumpy - Lots of extra warnings turned on

TARGET := Avida

# Identify all directory locations
EMP_DIR      = ../Empirical
BUILD_DIR    = build
WEB_DIR      = web
SETTINGS_DIR = settings
SOURCE_DIR   = source

CXX = g++
CXX_web := emcc

NATIVE_CODE = $(SOURCE_DIR)/$(TARGET).cpp
WEB_CODE = $(SOURCE_DIR)/$(TARGET)-web.cpp

NATIVE_EXE = $(BUILD_DIR)/$(TARGET)
WEB_EXE = $(WEB_DIR)/$(TARGET).js

# Specify sets of compilation flags to use
FLAGS_version := -std=c++20
FLAGS_warn    = -Wall -Wextra -Wno-unused-function -Wnon-virtual-dtor -Wcast-align -Woverloaded-virtual -pedantic
FLAGS_include = -I$(SOURCE_DIR)/ -I$(EMP_DIR)/include/
FLAGS_main    = $(FLAGS_version) $(FLAGS_warn) $(FLAGS_include) -pthread

# Emscripten / Empirical information
EMP_methods = -s EXPORTED_RUNTIME_METHODS="['ccall', 'cwrap', 'UTF8ToString', 'stringToUTF8', 'lengthBytesUTF8']"
EMP_funs = -s EXPORTED_FUNCTIONS="['_main', '_malloc', '_free', '_empCppCallback']"
EMP_js_lib = --js-library $(EMP_DIR)/emp/web/library_emp.js
EMP_limits = -s NO_EXIT_RUNTIME=1  -s TOTAL_MEMORY=67108864 -s DISABLE_EXCEPTION_CATCHING=1
FLAGS_emp := $(FLAGS_main) $(EMP_methods) $(EMP_js_lib) $(EMP_funs) $(EMP_limits)

FLAGS_QUICK    = $(FLAGS_main) -DNDEBUG
FLAGS_DEBUG    = $(FLAGS_main) -g -DEMP_TRACK_MEM
FLAGS_OPT      = $(FLAGS_main) -O3 -DNDEBUG
FLAGS_GRUMPY   = $(FLAGS_main) -DNDEBUG -Wconversion -Weffc++
FLAGS_COVERAGE = $(FLAGS_main)  -O0 -DEMP_TRACK_MEM -ftemplate-backtrace-limit=0 -fprofile-instr-generate -fcoverage-mapping -fno-inline -fno-elide-constructors

FLAGS_WEB       = $(FLAGS_emp) -Oz -DNDEBUG
FLAGS_WEB_DEBUG = $(FLAGS_emp) -g4 -pedantic -Wno-dollar-in-identifier-extension

native: FLAGS := $(FLAGS_OPT)
native: $(NATIVE_EXE)

default: native

debug: FLAGS := $(FLAGS_DEBUG)
debug: $(NATIVE_EXE)

grumpy: FLAGS := $(FLAGS_GRUMPY)
grumpy: $(NATIVE_EXE)

quick: FLAGS := $(FLAGS_QUICK)
quick: $(NATIVE_EXE)

web: FLAGS := $(FLAGS_WEB)
web: $(WEB_EXE)

debug-web: FLAGS := $(FLAGS_WEB_DEBUG)
debug-web: $(WEB_EXE)

all: native web

new: clean
new: native

# Debugging information
#print-%: ; @echo $*=$($*)
print-%: ; @echo '$(subst ','\'',$*=$($*))'

CLEAN_BACKUP = *~ *.dSYM
CLEAN_TEST = *.out	*.o	*.gcda	*.gcno	*.info	*.gcov	./Coverage* ./temp
CLEAN_EXE = $(NATIVE_EXE) $(WEB_EXE)

CLEAN_FILES = $(CLEAN_BACKUP) $(CLEAN_TEST) $(CLEAN_EXE)

clean:
	@echo Removing:
	@echo $(wildcard $(CLEAN_FILES))
	@echo ----
	rm -rf $(wildcard $(CLEAN_FILES))

# Make sure that the needed directories exists.
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
$(WEB_DIR):
	mkdir -p $(WEB_DIR)

# Compile the command-line version.
$(NATIVE_EXE): $(NATIVE_CODE) | $(BUILD_DIR)
	$(CXX) $(FLAGS) $(NATIVE_CODE) -o $(NATIVE_EXE)
	@echo To build the web version use: make web

# Compile the web version.
$(WEB_EXE): $(WEB_CODE) | $(WEB_DIR)
	$(CXX_web) $(FLAGS) $(WEB_CODE) -o $(WEB_EXE)
