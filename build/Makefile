# Relay all make targets to the root Avida Makefile
ROOT_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))..

%:
	$(MAKE) -C $(ROOT_DIR) $@

.DEFAULT_GOAL := native
