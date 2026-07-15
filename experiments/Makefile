CXX ?= g++
CXXFLAGS ?= -I.. -std=c++17 -O2 -W -Wall -Wextra -pedantic

tests := osd_smoke_test tep_trace_test osd_operation_count_test osd_profile_test parity_delta_test candidate_stage_benchmark paged_candidate_test padding_state_test combined_decoder_benchmark native_parity_decoder_benchmark exact_early_stop_probe

.PHONY: all test benchmark-tep clean

all: $(tests)

test: $(tests)
	./osd_smoke_test
	python3 compare_tep_trace.py ./tep_trace_test
	python3 tep_delivery_modes.py --k 5 --order 3 --page-size 4 --blocks 2 --warmup-blocks 1 --repeats 1 --output-dir /tmp/tep-delivery-smoke
	./osd_operation_count_test
	python3 analyze_profile.py ./osd_profile_test
	./parity_delta_test
	./candidate_stage_benchmark
	./paged_candidate_test
	./padding_state_test
	./combined_decoder_benchmark
	./native_parity_decoder_benchmark
	./exact_early_stop_probe

native_parity_decoder_benchmark: native_parity_decoder_benchmark.cc
	$(CXX) $(CXXFLAGS) -pthread $< -o $@

benchmark-tep:
	python3 tep_delivery_modes.py --k 64 --order 4 --page-size 4096 --blocks 200 --warmup-blocks 2 --repeats 5 --output-dir results

clean:
	rm -f $(tests)
