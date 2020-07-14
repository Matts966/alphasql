# syntax = docker/dockerfile:experimental

FROM l.gcr.io/google/bazel:1.0.0 as builder

# Use gcc because clang can't build m4
RUN apt-get update && \
    apt-get install build-essential software-properties-common -y && \
    add-apt-repository ppa:ubuntu-toolchain-r/test -y && \
    apt-get update && \
    # Use gcc-9 for using std::filesystem api
    apt-get install --no-install-recommends -y make gcc-9 g++-9 && \
    update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-9 900 \
        --slave /usr/bin/g++ g++ /usr/bin/g++-9
ENV CC /usr/bin/gcc
COPY . /work/alphasql
WORKDIR /work/alphasql

RUN --mount=type=cache,target=/root/.cache \
    bazel build //alphasql:all && \
    cp ./bazel-bin/alphasql/dag . && \
    cp ./bazel-bin/alphasql/pipeline_type_checker .


FROM gcr.io/distroless/cc
COPY --from=builder /work/alphasql/dag /usr/bin/dag
COPY --from=builder /work/alphasql/pipeline_type_checker /usr/bin/pipeline_type_checker
COPY --from=builder /usr/lib/x86_64-linux-gnu/libstdc++.so.6 /usr/lib/x86_64-linux-gnu/libstdc++.so.6
WORKDIR /home
