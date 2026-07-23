# Debug report build. Same pattern as the main report: latexmk into build/,
# dash check last.

BUILD := build
MAIN := debug_report

.PHONY: all clean
all:
	mkdir -p $(BUILD)
	latexmk -pdf -interaction=nonstopmode -halt-on-error \
	    -output-directory=$(BUILD) $(MAIN).tex
	python3 ../scripts/check_no_dashes.py .
	@echo "built $(BUILD)/$(MAIN).pdf"

clean:
	latexmk -C -output-directory=$(BUILD) $(MAIN).tex 2>/dev/null || true
	rm -rf $(BUILD)
