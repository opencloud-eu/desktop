PYTHON_LINT_PATHS:="./**/*.py"

UV_BIN := $(shell command -v uv 2>/dev/null)
CURL_BIN := $(shell command -v curl 2>/dev/null)
WGET_BIN := $(shell command -v wget 2>/dev/null)
PATH := $(PATH)

ifeq ($(UV_BIN),)
	PATH := $(HOME)/.local/bin:$(PATH)
	ifeq ($(CURL_BIN),)
		ifeq ($(WGET_BIN),)
			$(error "[ERR] Neither 'uv', 'curl', nor 'wget' command is available. Please install one of them.")
		endif
	endif
endif
export PATH

ifeq ($(CURL_BIN),)
	REQ_CMD := $(WGET_BIN) -qO-
else
	REQ_CMD := $(CURL_BIN) -LsSf
endif

.PHONY: install
install: uv-install install-chromium

.PHONY: check-uv
check-uv:
	@if [ -z "$(UV_BIN)" ]; then \
		echo "[INFO] 'uv' command not found. Installing uv..."; \
		$(REQ_CMD) https://astral.sh/uv/install.sh | sh; \
	fi

.PHONY: uv-install
uv-install: check-uv
	uv venv --system-site-packages --allow-existing
	uv sync --frozen

.PHONY: install-chromium
install-chromium: check-uv
	uv run playwright install chromium

.PHONY: run-atpsi-webdriver
run-atpsi-webdriver: check-uv
	bash woodpecker/run_atspi_webdriver.sh

.PHONY: python-lint
python-lint:
	black --check --diff .
	python3 -m pylint --rcfile ./.pylintrc $(PYTHON_LINT_PATHS)

.PHONY: python-lint-fix
python-lint-fix:
	black .
	python3 -m pylint --rcfile ./.pylintrc $(PYTHON_LINT_PATHS)

.PHONY: gherkin-lint
gherkin-lint:
	gherlint -c ./config/.gherlintrc.json .

.PHONY: gherkin-lint-fix
gherkin-lint-fix:
	gherlint --fix -c ./config/.gherlintrc.json .
