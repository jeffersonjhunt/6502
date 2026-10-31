# The books (README.md): the drift check, its own tests, the indexes.
#
#   make check      # tools/check_docs.py, then tools/test_check_docs.py
#   make coverage   # what is written, and what isn't yet
#   make index      # regenerate index/*.json from the entries
#
# The examples are built and run by the emulator's test_docs_examples
# (examples/Makefile builds them on their own).

PYTHON = python3 -I -B

check:
	$(PYTHON) tools/check_docs.py
	$(PYTHON) tools/test_check_docs.py

coverage:
	$(PYTHON) tools/check_docs.py --coverage

index:
	$(PYTHON) tools/check_docs.py --write-index

.PHONY: check coverage index
