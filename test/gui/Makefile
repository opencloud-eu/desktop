PYTHON_LINT_PATHS:="./**/*.py"

.PHONY: install
install: pip-install install-chromium

.PHONY: install-chromium
install-chromium:
	playwright install chromium

.PHONY: pip-install
pip-install:
	python3 -m pip install -r requirements.txt

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
