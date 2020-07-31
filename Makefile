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
	ls -d samples/*/ | while read sample; do \
		echo ""; \
		dag $$sample --output_path $$sample/dag.dot; \
		dot -Tpng $$sample/dag.dot -o $$sample/dag.png; \
		pipeline_type_checker $$sample/dag.dot \
			--json_schema_path ./samples/sample-schema.json; \
	done;

.PHONY: test
test: osx
	CC=g++ bazelisk test //alphasql:all
