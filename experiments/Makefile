CXX ?= g++
CXXFLAGS ?= -I.. -std=c++17 -O2 -W -Wall -Wextra -pedantic

tests := osd_smoke_test tep_trace_test osd_operation_count_test osd_profile_test parity_delta_test candidate_stage_benchmark paged_candidate_test

.PHONY: all test clean

all: $(tests)

test: $(tests)
	./osd_smoke_test
	python3 compare_tep_trace.py ./tep_trace_test
	./osd_operation_count_test
	python3 analyze_profile.py ./osd_profile_test
	./parity_delta_test
	./candidate_stage_benchmark
	./paged_candidate_test

clean:
	rm -f $(tests)
