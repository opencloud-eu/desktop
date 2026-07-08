PYTHON_LINT_PATHS:="./**/*.py"

.PHONY: install
install: uv-install install-chromium

.PHONY: uv-install
uv-install:
	uv venv --system-site-packages --allow-existing
	uv sync --frozen	
	
.PHONY: install-chromium
install-chromium:
	uv run playwright install chromium

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
