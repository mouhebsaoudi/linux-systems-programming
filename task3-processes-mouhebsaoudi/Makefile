# Set you prefererred CFLAGS/compiler compiler here.
# Our github runner provides gcc-10 by default.
CC ?= cc
CFLAGS ?= -g -Wall -O2
CXX ?= c++
CXXFLAGS ?= -g -Wall -O2
CARGO ?= cargo
RUSTFLAGS ?= -g
LDFLAGS ?= -pthread
# CPPFLAGS += -DYYDEBUG -DDEBUG

all:
	echo "Include either c.make or rust.make"

 C/C++ example:
include c.make

# Rust example:
#include rust.make

# this target should build all executables for all tests
#all:
#	@echo "Please set a concrete build command here"
#	false

# Usually there is no need to modify this
check: all
	$(MAKE) -C tests check
