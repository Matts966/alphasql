.PHONY: build-and-check
build-and-check: test
	make sample

.PHONY: osx
osx:
	CC=g++ bazelisk build //alphasql:all
	sudo cp ./bazel-bin/alphasql/alphadag /usr/local/bin
	sudo cp ./bazel-bin/alphasql/alphacheck /usr/local/bin

.PHONY: sample
sample: osx
	ls -d samples/*/ | while read sample_path; do \
		echo ""; \
		alphadag $$sample_path --output_path $$sample_path/dag.dot \
			--external_required_tables_output_path $$sample_path/external_tables.txt \
		 	> $$sample_path/alphadag_stdout.txt 2> $$sample_path/alphadag_stderr.txt; \
		dot -Tpng $$sample_path/dag.dot -o $$sample_path/dag.png; \
		alphacheck $$sample_path/dag.dot \
			--json_schema_path ./samples/sample-schema.json \
			> $$sample_path/alphacheck_stdout.txt 2> $$sample_path/alphacheck_stderr.txt; \
	done;

.PHONY: test
test: osx
	CC=g++ bazelisk test //alphasql:all
