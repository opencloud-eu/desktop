PATH := $(PATH)
ifeq ($(OS),Windows_NT)
  PATH := $(USERPROFILE)/.local/bin:$(PATH)
  UV_BIN := $(shell powershell.exe (Get-Command -Name uv -CommandType Application).Source 2>$nul)
  WGET_BIN := $(shell powershell.exe (Get-Command -Name wget -CommandType Application).Source 2>$nul)
  CURL_BIN := $(shell powershell.exe (Get-Command -Name curl -CommandType Application).Source 2>$nul)
else
  PATH := $(HOME)/.local/bin:$(PATH)
  UV_BIN := $(shell command -v uv 2>/dev/null)
  CURL_BIN := $(shell command -v curl 2>/dev/null)
  WGET_BIN := $(shell command -v wget 2>/dev/null)
endif

ifeq ($(UV_BIN),)
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

ifeq ($(UV_BIN),)
  $(info "[INFO] 'uv' command not found. Installing uv...")
  ifeq ($(OS),Windows_NT)
    $(shell powershell -ExecutionPolicy ByPass -c "irm https://astral.sh/uv/0.13.1/install.ps1 | iex" > $null 2>&1)
  else
    $(shell $(REQ_CMD) https://astral.sh/uv/install.sh | sh >/dev/null 2>&1)
  endif
endif

.PHONY: install
install: uv-install install-chromium

.PHONY: uv-install
uv-install:
  uv venv --system-site-packages --allow-existing
  uv sync --frozen --all-groups

.PHONY: install-chromium
install-chromium:
  uv run playwright install chromium

.PHONY: run-atpsi-webdriver
run-atpsi-webdriver:
  bash woodpecker/run_atspi_webdriver.sh

.PHONY: python-lint
python-lint:
  uv run --no-sync ruff format . --diff
  uv run --no-sync ruff check .

.PHONY: python-lint-fix
 python-lint-fix:
  uv run --no-sync ruff format .

.PHONY: check-dead-code
check-dead-code:
  uv run --no-sync vulture .

.PHONY: gherkin-lint
gherkin-lint:
  gherlint -c ./config/.gherlintrc.json .

.PHONY: gherkin-lint-fix
gherkin-lint-fix:
  gherlint --fix -c ./config/.gherlintrc.json .
