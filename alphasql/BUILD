#
# Copyright 2020 Matts966
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

package(
    default_visibility = ["//:__subpackages__"],
)

cc_library(
    name = "json_schema_reader",
    hdrs = ["json_schema_reader.h"],
    deps = [
        "@com_google_zetasql//zetasql/public:simple_catalog",
        "@com_google_zetasql//zetasql/public:type",
        "@boost//:property_tree",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "identifier_resolver",
    hdrs = ["identifier_resolver.h"],
    deps = [
        "@com_google_zetasql//zetasql/public:simple_catalog",
        "@com_google_zetasql//zetasql/public:type",
        "@boost//:property_tree",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "function_name_resolver",
    hdrs = ["function_name_resolver.h"],
    srcs = ["function_name_resolver.cc"],
    deps = [
        "@com_google_zetasql//zetasql/public:simple_catalog",
        "@com_google_zetasql//zetasql/public:type",
        "@com_google_zetasql//zetasql/public:analyzer",
        "@com_google_zetasql//zetasql/public:language_options",
        "@com_google_zetasql//zetasql/parser:parser",
        "@com_google_zetasql//zetasql/analyzer",
        "@boost//:property_tree",
        "@com_google_absl//absl/strings",
    ],
)

cc_binary(
    name = "pipeline_type_checker",
    srcs = [
        "pipeline_type_checker.cc",
    ],
    deps = [
        ":json_schema_reader",
        "@com_google_zetasql//zetasql/base",
        "@com_google_zetasql//zetasql/base:map_util",
        "@com_google_zetasql//zetasql/base:ret_check",
        "@com_google_zetasql//zetasql/base:source_location",
        "@com_google_zetasql//zetasql/base:status",
        "@com_google_zetasql//zetasql/base:statusor",
        "@com_google_zetasql//zetasql/public:analyzer",
        "@com_google_zetasql//zetasql/public:catalog",
        "@com_google_zetasql//zetasql/public:evaluator",
        "@com_google_zetasql//zetasql/public:evaluator_table_iterator",
        "@com_google_zetasql//zetasql/public:language_options",
        "@com_google_zetasql//zetasql/public:simple_catalog",
        "@com_google_zetasql//zetasql/public:type",
        "@com_google_zetasql//zetasql/public:type_cc_proto",
        "@com_google_zetasql//zetasql/public:value",
        "@com_google_zetasql//zetasql/resolved_ast",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:cord",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/types:span",
        "@com_google_protobuf//:protobuf",
        "@boost//:graph",
    ],
)

cc_binary(
    name = "dag",
    srcs = ["dag.cc"],
    deps = [
        ":dag_lib",
    ],
)


cc_library(
    name = "dag_lib",
    hdrs = ["dag_lib.h"],
    deps = [
        "@com_google_zetasql//zetasql/analyzer",
        "@com_google_zetasql//zetasql/resolved_ast",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/strings",
        "@boost//:graph",
        ":identifier_resolver",
        ":function_name_resolver",
    ],
)

cc_test(
    name = "dag_lib_test",
    srcs = ["dag_lib_test.cc"],
    deps = [
        ":dag_lib",
        "@com_google_googletest//:gtest_main",
        "@boost//:graph",
    ],
)
