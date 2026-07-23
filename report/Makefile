# Main report build. latexmk into build/, then the dash check runs last so a
# stray en or em dash in the compiled prose fails the build (Section 13).

BUILD := build
MAIN := main

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
