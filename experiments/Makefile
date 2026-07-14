CXX ?= g++
CXXFLAGS ?= -I.. -std=c++17 -O2 -W -Wall -Wextra -pedantic

tests := osd_smoke_test tep_trace_test

.PHONY: all test clean

all: $(tests)

test: $(tests)
	./osd_smoke_test
	python3 compare_tep_trace.py ./tep_trace_test

clean:
	rm -f $(tests)
