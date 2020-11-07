.PHONY: build-and-check
build-and-check: test
	make sample

.PHONY: osx
osx:
	CC=g++ bazelisk build //alphasql:all
	sudo cp ./bazel-bin/alphasql/dag /usr/local/bin
	sudo cp ./bazel-bin/alphasql/pipeline_type_checker /usr/local/bin

.PHONY: sample
sample: osx
	color() { \
		set -o pipefail; "$$@" 2>&1>&3|sed $$'s,.*,\e[31m&\e[m,'>&2; \
	} 3>&1 && \
	ls -d samples/*/ | while read sample_path; do \
		echo ""; \
		dag $$sample_path --output_path $$sample_path/dag.dot \
		--external_required_tables_output_path $$sample_path/external_tables.txt; \
		dot -Tpng $$sample_path/dag.dot -o $$sample_path/dag.png; \
		color pipeline_type_checker $$sample_path/dag.dot \
			--json_schema_path ./samples/sample-schema.json; \
	done;

.PHONY: test
test: osx
	CC=g++ bazelisk test //alphasql:all
