.PHONY: build-and-check
build-and-check: test
	make sample

.PHONY: osx
osx:
	bazelisk build //alphasql:all
	sudo cp ./bazel-bin/alphasql/dag /usr/local/bin
	sudo cp ./bazel-bin/alphasql/pipeline_type_checker /usr/local/bin

.PHONY: sample
sample: osx
	color() { \
		set -o pipefail; "$$@" 2>&1>&3|sed $$'s,.*,\e[31m&\e[m,'>&2; \
	} 3>&1 && \
	ls -d samples/*/ | while read sample; do \
		echo ""; \
		dag $$sample --output_path $$sample/dag.dot; \
		dot -Tpng $$sample/dag.dot -o $$sample/dag.png; \
		color pipeline_type_checker $$sample/dag.dot \
			--json_schema_path ./samples/sample-schema.json; \
	done;

.PHONY: test
test: osx
	bazelisk test //alphasql:all
