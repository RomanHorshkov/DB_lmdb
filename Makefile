# --- clang-format detection and helpers ---
CLANG_FORMAT := $(shell command -v clang-format 2>/dev/null)
FMT_ROOT := ./app/
FMT_FIND := find $(FMT_ROOT) \( -path "$(FMT_ROOT)/app/external" -o -path "$(FMT_ROOT)/build" \) -prune -o -type f \( -name '*.c' -o -name '*.h' \) -print0

.PHONY: format

format:
ifeq ($(strip $(CLANG_FORMAT)),)
	@echo "clang-format not found. Install it (e.g., sudo apt install clang-format)"; exit 1
endif
	@test -f .clang-format || { echo ".clang-format missing at repo root"; exit 1; }
	@cnt=`$(FMT_FIND) | tr -cd '\0' | wc -c`; \
	if [ $$cnt -eq 0 ]; then \
	  echo "[fmt] no files"; \
	else \
	  echo "[fmt] formatting $$cnt files under $(FMT_ROOT) (excluding app/external and build)"; \
	  $(FMT_FIND) | xargs -0 -r $(CLANG_FORMAT) -i -style=file; \
	fi
