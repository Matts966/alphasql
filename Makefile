update: osx linux
	make push
	@echo "all artifacts are updated"
run: build
	docker run -it --rm -v `pwd`:/home:Z matts966/alphasql:latest
build:
	DOCKER_BUILDKIT=1 docker build -t matts966/alphasql:latest -f ./docker/Dockerfile .
push: build
	docker push matts966/alphasql:latest
osx:
	CC=g++ bazel build //zetasql/experimental:all
	sudo cp ./bazel-bin/zetasql/experimental/dag ./bin/osx/dag
	sudo cp ./bazel-bin/zetasql/experimental/pipeline_type_checker ./bin/osx/pipeline_type_checker
	sudo cp ./bin/osx/dag /usr/local/bin
	sudo cp ./bin/osx/pipeline_type_checker /usr/local/bin
linux: build
	./docker/linux-copy-bin.sh
.PHONY: run build osx push
