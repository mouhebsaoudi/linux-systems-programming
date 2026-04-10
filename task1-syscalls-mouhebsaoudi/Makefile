# Set you prefererred CFLAGS/compiler compiler here.
# Our github runner provides gcc-10 by default.
CC ?= cc
CFLAGS ?= -g -Wall -O2
CXX ?= c++
CXXFLAGS ?= -g -Wall -O2
CARGO ?= cargo
RUSTFLAGS ?= -g

# this target should build all executables for all tests
all:
	@echo "$(CC) -D_GNU_SOURCE -O2 -Wall -Wextra -c $< -o $@"
	true

# C example:
all: librw_1.so librw_2.so tracer
librw_1.so: task-1_1.c
	$(CC) $(CFLAGS) -shared -fPIC -ldl -o $@ $<

librw_2.so: task-1_2.c
	$(CC) $(CFLAGS) -shared -fPIC -ldl -o $@ $<

tracer: tracer.o
	$(CC) $(CFLAGS) -o $@ $^

tracer.o: tracer.c
	$(CC) $(CFLAGS) -D_GNU_SOURCE -c $< -o $@




# Rust example:
#all:
#	$(CARGO) build --release

# Usually there is no need to modify this
check: all
	$(MAKE) -C tests check
