## Isolation Alloc Makefile
## Copyright 2020 - chris.rohlf@gmail.com

CC = clang
CXX = clang++

## Security flags can affect performance
## SANITIZE_CHUNKS - Clear user chunks upon free
## FUZZ_MODE - Call verify_all_zones upon alloc/free, never reuse custom zones
## PERM_FREE_REALLOC - Permanently free any realloc'd chunk
SECURITY_FLAGS = -DSANITIZE_CHUNKS=0 -DFUZZ_MODE=0 -DPERM_FREE_REALLOC=0

## This enables Address Sanitizer support for manually
## poisoning and unpoisoning zones. It adds a significant
## performance and memory penalty. If you want to enable
## this just uncomment the line below
#ENABLE_ASAN = -fsanitize=address -DENABLE_ASAN=1

## Enable memory sanitizer to catch uninitialized reads.
## This is slow, and it's incompatible with ENABLE_ASAN
#ENABLE_MSAN = -fsanitize=memory -fsanitize-memory-track-origins

SANITIZER_SUPPORT = $(ENABLE_ASAN) $(ENABLE_MSAN)

## Support for threads adds a performance overhead
## You can safely disable it here if you know your
## program does not require concurrent access
## to the IsoAlloc APIs
THREAD_SUPPORT = -DTHREAD_SUPPORT=1 -pthread

## Instructs the kernel (via mmap) to prepopulate
## page tables which will reduce page faults and
## improve performance. If you're using IsoAlloc
## for small short lived programs you probably
## want to disable this. This is ignored on MacOS
PRE_POPULATE_PAGES = -DPRE_POPULATE_PAGES=1

## Enable some functionality that like IsoAlloc internals
## for tests that need to verify security properties
UNIT_TESTING = -DUNIT_TESTING=1

## Enable the malloc/free and new/delete hooks
## This is not recommended. The IsoAlloc APIs
## should be called directly instead
#MALLOC_HOOK = -DMALLOC_HOOK=1

HOOKS = $(MALLOC_HOOK)

OPTIMIZE = -O2
COMMON_CFLAGS = -Wall -Iinclude/ $(THREAD_SUPPORT) $(PRE_POPULATE_PAGES)
BUILD_ERROR_FLAGS = -Werror -pedantic -Wno-pointer-arith -Wno-gnu-zero-variadic-macro-arguments -Wno-format-pedantic
CFLAGS = $(COMMON_CFLAGS) $(SECURITY_FLAGS) $(BUILD_ERROR_FLAGS) $(HOOKS) -fvisibility=hidden -std=c11 $(SANITIZER_SUPPORT)
CXXFLAGS = $(COMMON_CFLAGS) -DCPP_SUPPORT=1 -std=c++17 $(SANITIZER_SUPPORT) $(HOOKS)
EXE_CFLAGS = -fPIE
DEBUG_FLAGS = -DDEBUG=1 -DLEAK_DETECTOR=1 -DMEM_USAGE=1
GDB_FLAGS = -g -ggdb3 -fno-omit-frame-pointer
PERF_FLAGS = -pg -DPERF_BUILD
LIBRARY = -fPIC -shared
SRC_DIR = src
C_SRCS = $(SRC_DIR)/*.c
CXX_SRCS = $(SRC_DIR)/*.cpp
BUILD_DIR = build
LDFLAGS = -L$(BUILD_DIR) -lisoalloc

UNAME := $(shell uname)
ifeq ($(UNAME), Darwin)
OS_FLAGS = -framework Security
endif

all: library tests

## Build a release version of the library
library: clean
	@echo "make library"
	$(CC) $(CFLAGS) $(LIBRARY) $(OPTIMIZE) $(OS_FLAGS) $(C_SRCS) -o $(BUILD_DIR)/libisoalloc.so

## Build a debug version of the library
library_debug: clean
	@echo "make library debug"
	$(CC) $(CFLAGS) $(LIBRARY) $(OS_FLAGS) $(DEBUG_FLAGS) $(GDB_FLAGS) $(C_SRCS) -o $(BUILD_DIR)/libisoalloc.so

## Build a debug version of the library
## specifically for unit tests
library_debug_unit_tests: clean
	@echo "make library_debug_unit_tests"
	$(CC) $(CFLAGS) $(LIBRARY) $(OS_FLAGS) $(UNIT_TESTING) $(DEBUG_FLAGS) $(GDB_FLAGS) $(C_SRCS) -o $(BUILD_DIR)/libisoalloc.so

## Builds a debug version of the library with scan-build
## Requires scan-build is in your PATH
analyze_library_debug: clean
	@echo "make analyze_library_debug"
	scan-build $(CC) $(CFLAGS) $(LIBRARY) $(OS_FLAGS) $(DEBUG_FLAGS) $(GDB_FLAGS) $(C_SRCS) -o $(BUILD_DIR)/libisoalloc.so

## Build a debug version of the library
library_debug_no_output: clean
	@echo "make library_debug_no_output"
	$(CC) $(CFLAGS) $(LIBRARY) $(OS_FLAGS) $(GDB_FLAGS) $(C_SRCS) -o $(BUILD_DIR)/libisoalloc.so

## C++ Support - Build object files for C code
c_library_objects:
	@echo "make c_library_objects"
	$(CC) $(CFLAGS) $(OPTIMIZE) $(C_SRCS) -fPIC -c
	mv *.o $(BUILD_DIR)

## C++ Support - Build debug object files for C code
c_library_objects_debug:
	@echo "make c_library_objects_debug"
	$(CC) $(CFLAGS) $(C_SRCS) $(DEBUG_FLAGS) -fPIC -c
	mv *.o $(BUILD_DIR)

## C++ Support - Build the library with C++ support
cpp_library: clean c_library_objects
	@echo "make cpp_library"
	$(CXX) $(CXXFLAGS) $(OPTIMIZE) $(LIBRARY) $(OS_FLAGS) $(CXX_SRCS) $(BUILD_DIR)/*.o -o $(BUILD_DIR)/libisoalloc.so

cpp_library_debug: clean c_library_objects_debug
	@echo "make cpp_library_debug"
	$(CXX) $(CXXFLAGS) $(DEBUG_FLAGS) $(LIBRARY) $(OS_FLAGS) $(CXX_SRCS) $(BUILD_DIR)/*.o -o $(BUILD_DIR)/libisoalloc.so

## Build a debug version of the unit test
tests: clean library_debug_unit_tests
	@echo "make library_debug_unit_tests"
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) $(GDB_FLAGS) $(UNIT_TESTING) tests/big_canary_test.c -o $(BUILD_DIR)/big_canary_test $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) $(GDB_FLAGS) tests/tests.c -o $(BUILD_DIR)/tests $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) $(GDB_FLAGS) tests/big_tests.c -o $(BUILD_DIR)/big_tests $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) $(GDB_FLAGS) tests/double_free.c -o $(BUILD_DIR)/double_free $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) $(GDB_FLAGS) tests/heap_overflow.c -o $(BUILD_DIR)/heap_overflow $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) $(GDB_FLAGS) tests/heap_underflow.c -o $(BUILD_DIR)/heap_underflow $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) $(GDB_FLAGS) tests/interfaces_test.c -o $(BUILD_DIR)/interfaces_test $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) $(GDB_FLAGS) tests/thread_tests.c -o $(BUILD_DIR)/thread_tests $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) $(GDB_FLAGS) tests/leaks_test.c -o $(BUILD_DIR)/leaks_test $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) $(GDB_FLAGS) tests/wild_free.c -o $(BUILD_DIR)/wild_free $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) $(GDB_FLAGS) tests/unaligned_free.c -o $(BUILD_DIR)/unaligned_free $(LDFLAGS)
	$(CC) $(CFLAGS) $(EXE_CFLAGS) $(DEBUG_FLAGS) $(GDB_FLAGS) tests/incorrect_chunk_size_multiple.c -o $(BUILD_DIR)/incorrect_chunk_size_multiple $(LDFLAGS)
	utils/run_tests.sh

fuzz_test: clean
	@echo "make fuzz_test"
	$(CC) $(COMMON_CFLAGS) $(C_SRCS) $(DEBUG_FLAGS) $(GDB_FLAGS) $(OS_FLAGS) -DNEVER_REUSE_ZONES=1 tests/alloc_fuzz.c -o $(BUILD_DIR)/alloc_fuzz
	LD_LIBRARY_PATH=build/ build/alloc_fuzz

## Build a non-debug library with performance
## monitoring enabled. Linux only
perf_tests: clean
	@echo "make perf_tests"
	$(CC) $(CFLAGS) $(C_SRCS) $(OPTIMIZE) $(PERF_FLAGS) tests/tests.c -o $(BUILD_DIR)/tests_gprof
	$(CC) $(CFLAGS) $(C_SRCS) $(OPTIMIZE) $(PERF_FLAGS) tests/big_tests.c -o $(BUILD_DIR)/big_tests_gprof
	$(BUILD_DIR)/tests_gprof
	gprof -b $(BUILD_DIR)/tests_gprof gmon.out > tests_perf_analysis.txt
	$(BUILD_DIR)/big_tests_gprof
	gprof -b $(BUILD_DIR)/big_tests_gprof gmon.out > big_tests_perf_analysis.txt

## Runs a single test that prints CPU time
## compared to the same malloc/free operations
malloc_cmp_test: clean
	@echo "make malloc_cmp_test"
	$(CC) $(CFLAGS) $(C_SRCS) $(OPTIMIZE) $(EXE_CFLAGS) $(OS_FLAGS) tests/tests.c -o $(BUILD_DIR)/tests
	$(CC) $(CFLAGS) $(C_SRCS) $(OPTIMIZE) $(EXE_CFLAGS) $(OS_FLAGS) -DMALLOC_PERF_TEST tests/tests.c -o $(BUILD_DIR)/malloc_tests
	echo "Running IsoAlloc Performance Test"
	build/tests
	echo "Running glibc malloc Performance Test"
	build/malloc_tests

## C++ Support - Build a debug version of the unit test
cpp_tests: clean cpp_library_debug
	@echo "make cpp_tests"
	$(CXX) $(CXXFLAGS) $(DEBUG_FLAGS) $(EXE_CFLAGS) tests/tests.cpp -o $(BUILD_DIR)/cxx_tests $(LDFLAGS)
	LD_LIBRARY_PATH=$(BUILD_DIR)/ LD_PRELOAD=$(BUILD_DIR)/libisoalloc.so $(BUILD_DIR)/cxx_tests

install:
	cp -pR build/libisoalloc.so /usr/lib/

format:
	clang-format $(SRC_DIR)/*.* tests/*.* include/*.h -i

clean:
	rm -rf build/* tests_perf_analysis.txt big_tests_perf_analysis.txt gmon.out test_output.txt *.dSYM core*
	mkdir -p build/
