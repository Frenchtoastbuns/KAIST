CXX ?= g++
CXXFLAGS ?= -I.. -std=c++17 -O2 -W -Wall -Wextra -pedantic

tests := osd_smoke_test

.PHONY: all test clean

all: $(tests)

test: $(tests)
	./osd_smoke_test

clean:
	rm -f $(tests)
