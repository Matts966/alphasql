update: osx linux
	make push
	make sample
	@echo "all artifacts are updated"
run: build
	docker run -it --rm -v `pwd`:/home:Z matts966/alphasql:latest
build:
	DOCKER_BUILDKIT=1 docker build -t matts966/alphasql:latest -f ./docker/Dockerfile .
push: build
	docker push matts966/alphasql:latest
osx:
	CC=g++ bazel build //alphasql:all
	sudo cp ./bazel-bin/alphasql/dag ./bin/osx/dag
	sudo cp ./bazel-bin/alphasql/pipeline_type_checker ./bin/osx/pipeline_type_checker
	sudo cp ./bin/osx/dag /usr/local/bin
	sudo cp ./bin/osx/pipeline_type_checker /usr/local/bin
sample:
	dag ./samples/sample1/ --output_path ./samples/sample1/dag.dot
	dot -Tpng ./samples/sample1/dag.dot -o ./samples/sample1/dag.png
	pipeline_type_checker ./samples/sample1/dag.dot

	dag ./samples/sample-arbitrary-dependency-graph-with-drop-statement/ --output_path \
		./samples/sample-arbitrary-dependency-graph-with-drop-statement/dag.dot
	dot -Tpng ./samples/sample-arbitrary-dependency-graph-with-drop-statement/dag.dot \
		-o ./samples/sample-arbitrary-dependency-graph-with-drop-statement/dag.png
	pipeline_type_checker ./samples/sample-arbitrary-dependency-graph-with-drop-statement/dag.dot \
		--json_schema_path ./samples/sample-arbitrary-dependency-graph-with-drop-statement/schema.json

	dag ./samples/sample-cycle/ --output_path ./samples/sample-cycle/dag.dot
	dot -Tpng ./samples/sample-cycle/dag.dot -o ./samples/sample-cycle/dag.png
	pipeline_type_checker ./samples/sample-cycle/dag.dot

	dag ./samples/sample2/ --output_path ./samples/sample2/dag.dot
	dot -Tpng ./samples/sample2/dag.dot -o ./samples/sample2/dag.png
	pipeline_type_checker ./samples/sample2/dag.dot

linux: build
	./docker/linux-copy-bin.sh
.PHONY: run build osx push
