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
		alphadag --with_functions $$sample_path --output_path $$sample_path/with_funtcions/dag.dot \
			--external_required_tables_output_path $$sample_path/with_funtcions/external_tables.txt \
		 	> $$sample_path/with_funtcions/alphadag_stdout.txt 2> $$sample_path/with_funtcions/alphadag_stderr.txt; \
		dot -Tpng $$sample_path/with_funtcions/dag.dot -o $$sample_path/with_funtcions/dag.png; \
		alphadag --with_tables $$sample_path --output_path $$sample_path/with_tables/dag.dot \
			--external_required_tables_output_path $$sample_path/with_tables/external_tables.txt \
		 	> $$sample_path/with_tables/alphadag_stdout.txt 2> $$sample_path/with_tables/alphadag_stderr.txt; \
		dot -Tpng $$sample_path/with_tables/dag.dot -o $$sample_path/with_tables/dag.png; \
		alphadag --with_tables --with_functions $$sample_path --output_path $$sample_path/with_all/dag.dot \
			--external_required_tables_output_path $$sample_path/with_all/external_tables.txt \
		 	> $$sample_path/with_all/alphadag_stdout.txt 2> $$sample_path/with_all/alphadag_stderr.txt; \
		dot -Tpng $$sample_path/with_all/dag.dot -o $$sample_path/with_all/dag.png; \
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
