.PHONY: build-and-check
build-and-check: build
	make test
	make samples

.PHONY: build
build:
	bazel build //alphasql:all
	cp ./bazel-bin/alphasql/alphadag /usr/local/bin
	chmod +x /usr/local/bin/alphadag
	cp ./bazel-bin/alphasql/alphacheck /usr/local/bin
	chmod +x /usr/local/bin/alphacheck

.PHONY: samples
samples: without_options with_functions with_tables with_all side_effect_first side_effect_first_with_tables

.PHONY: without_options
without_options:
	find samples -mindepth 1 -maxdepth 1 -type d | while read sample_path; do \
		alphadag $$sample_path --output_path $$sample_path/dag.dot \
			--external_required_tables_output_path $$sample_path/external_tables.txt \
		  > $$sample_path/alphadag_stdout.txt 2> $$sample_path/alphadag_stderr.txt; \
		dot -Tpng $$sample_path/dag.dot -o $$sample_path/dag.png; \
		alphacheck $$sample_path/dag.dot \
			--json_schema_path ./samples/sample-schema.json \
			> $$sample_path/alphacheck_stdout.txt 2> $$sample_path/alphacheck_stderr.txt || echo alphacheck $$sample_path failed; \
	done;

.PHONY: with_functions
with_functions:
	find samples -mindepth 1 -maxdepth 1 -type d | while read sample_path; do \
	  mkdir -p $$sample_path/with_functions; \
		alphadag --with_functions $$sample_path --output_path $$sample_path/with_functions/dag.dot \
      --external_required_tables_output_path $$sample_path/with_functions/external_tables.txt \
      > $$sample_path/with_functions/alphadag_stdout.txt 2> $$sample_path/with_functions/alphadag_stderr.txt; \
		dot -Tpng $$sample_path/with_functions/dag.dot -o $$sample_path/with_functions/dag.png; \
		alphacheck $$sample_path/with_functions/dag.dot \
			--json_schema_path ./samples/sample-schema.json \
			> $$sample_path/with_functions/alphacheck_stdout.txt 2> $$sample_path/with_functions/alphacheck_stderr.txt || echo alphacheck --with_functions $$sample_path failed; \
	done;

.PHONY: with_tables
with_tables:
	find samples -mindepth 1 -maxdepth 1 -type d | while read sample_path; do \
	  mkdir -p $$sample_path/with_tables; \
    alphadag --with_tables $$sample_path --output_path $$sample_path/with_tables/dag.dot \
      --external_required_tables_output_path $$sample_path/with_tables/external_tables.txt \
      > $$sample_path/with_tables/alphadag_stdout.txt 2> $$sample_path/with_tables/alphadag_stderr.txt; \
    dot -Tpng $$sample_path/with_tables/dag.dot -o $$sample_path/with_tables/dag.png; \
		alphacheck $$sample_path/with_tables/dag.dot \
			--json_schema_path ./samples/sample-schema.json \
			> $$sample_path/with_tables/alphacheck_stdout.txt 2> $$sample_path/with_tables/alphacheck_stderr.txt || echo alphacheck --with_tables $$sample_path failed; \
	done;

.PHONY: with_all
with_all:
	find samples -mindepth 1 -maxdepth 1 -type d | while read sample_path; do \
	  mkdir -p $$sample_path/with_all; \
		alphadag --with_tables --with_functions $$sample_path --output_path $$sample_path/with_all/dag.dot \
			--external_required_tables_output_path $$sample_path/with_all/external_tables.txt \
		 	> $$sample_path/with_all/alphadag_stdout.txt 2> $$sample_path/with_all/alphadag_stderr.txt; \
		dot -Tpng $$sample_path/with_all/dag.dot -o $$sample_path/with_all/dag.png; \
		alphacheck $$sample_path/with_all/dag.dot \
			--json_schema_path ./samples/sample-schema.json \
			> $$sample_path/with_all/alphacheck_stdout.txt 2> $$sample_path/with_all/alphacheck_stderr.txt || echo alphacheck with all resources $$sample_path failed; \
	done;

.PHONY: side_effect_first
side_effect_first:
	find samples -mindepth 1 -maxdepth 1 -type d | while read sample_path; do \
	  mkdir -p $$sample_path/side_effect_first; \
		alphadag --side_effect_first $$sample_path --output_path $$sample_path/side_effect_first/dag.dot \
			--external_required_tables_output_path $$sample_path/side_effect_first/external_tables.txt \
		 	> $$sample_path/side_effect_first/alphadag_stdout.txt 2> $$sample_path/side_effect_first/alphadag_stderr.txt; \
    dot -Tpng $$sample_path/side_effect_first/dag.dot -o $$sample_path/side_effect_first/dag.png; \
		alphacheck $$sample_path/side_effect_first/dag.dot \
			--json_schema_path ./samples/sample-schema.json \
			> $$sample_path/side_effect_first/alphacheck_stdout.txt 2> $$sample_path/side_effect_first/alphacheck_stderr.txt || echo alphacheck --side_effect_first $$sample_path failed; \
	done;

.PHONY: side_effect_first_with_tables
side_effect_first_with_tables:
	find samples -mindepth 1 -maxdepth 1 -type d | while read sample_path; do \
	  mkdir -p $$sample_path/side_effect_first_with_tables; \
	 	alphadag --side_effect_first --with_tables $$sample_path --output_path $$sample_path/side_effect_first_with_tables/dag.dot \
			--external_required_tables_output_path $$sample_path/side_effect_first_with_tables/external_tables.txt \
		 	> $$sample_path/side_effect_first_with_tables/alphadag_stdout.txt 2> $$sample_path/side_effect_first_with_tables/alphadag_stderr.txt; \
		dot -Tpng $$sample_path/side_effect_first_with_tables/dag.dot -o $$sample_path/side_effect_first_with_tables/dag.png; \
		alphacheck $$sample_path/side_effect_first_with_tables/dag.dot \
			--json_schema_path ./samples/sample-schema.json \
			> $$sample_path/side_effect_first_with_tables/alphacheck_stdout.txt 2> $$sample_path/side_effect_first_with_tables/alphacheck_stderr.txt || echo alphacheck --side_effect_first --with_tables $$sample_path failed; \
	done;

.PHONY: test
test:
	bazel test //alphasql:all
