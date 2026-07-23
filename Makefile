# CPU microbenchmark suite: top level orchestration.
#
# Every target is meant to run inside WSL2 Ubuntu (or a native Linux CI
# runner). `make all` reproduces the whole project from a clean tree:
# configure, build, test, run the suite, regenerate plots, compile both
# reports, and run the dash check last. The GPU is not used anywhere.

BUILD_DIR    := build
CMAKE        := cmake
CTEST        := ctest
PYTHON       := python3
JOBS         := $(shell nproc 2>/dev/null || echo 4)

.DEFAULT_GOAL := build

.PHONY: all build configure test suite plots report report-debug report-me \
        check-style ruff dashes clean help

help:
	@echo "Targets:"
	@echo "  build         configure and compile all benchmarks and tests"
	@echo "  test          run the unit and integration test suite (ctest)"
	@echo "  suite         run the full measurement suite (20 to 40 minutes)"
	@echo "  plots         regenerate figures and tables from summary.json"
	@echo "  report        build the main report PDF"
	@echo "  report-debug  build the engineering debug report PDF"
	@echo "  report-me     build the personal documentation PDF"
	@echo "  check-style   dash check plus ruff"
	@echo "  all           clean-tree reproduction of everything"
	@echo "  clean         remove build trees and generated artifacts"

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release

build: configure
	$(CMAKE) --build $(BUILD_DIR) -j $(JOBS)

test: build
	cd $(BUILD_DIR) && $(CTEST) --output-on-failure

suite: build
	bash scripts/run_suite.sh

plots:
	$(PYTHON) scripts/gen_report_assets.py

report:
	$(MAKE) -C report

report-debug:
	$(MAKE) -C report_debug

report-me:
	$(MAKE) -C report_for_me

# --------------------------------------------------------------------------
# Style gates. Runnable from Phase 0 with no build artifacts present.
# --------------------------------------------------------------------------
check-style: dashes ruff

dashes:
	$(PYTHON) scripts/check_no_dashes.py

ruff:
	ruff check scripts

all:
	$(MAKE) clean
	$(MAKE) build
	$(MAKE) test
	$(MAKE) suite
	$(MAKE) plots
	$(MAKE) report
	$(MAKE) report-debug
	$(MAKE) report-me
	$(MAKE) check-style
	@echo "make all complete."

clean:
	rm -rf $(BUILD_DIR)
	rm -rf report/build report_debug/build report_for_me/build
	find . -name '__pycache__' -type d -prune -exec rm -rf {} + 2>/dev/null || true
