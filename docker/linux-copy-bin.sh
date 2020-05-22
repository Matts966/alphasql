#!/bin/bash
set -ex
id=$(docker create matts966/alphasql /bin/bash)
docker cp $id:/usr/bin/dag ./bin/linux/dag
docker cp $id:/usr/bin/pipeline_type_checker ./bin/linux/pipeline_type_checker
docker rm -v $id
