//
// Copyright 2020 Matts966
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <filesystem>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "alphasql/identifier_resolver.h"
#include "boost/graph/depth_first_search.hpp"
#include "boost/graph/graphviz.hpp"
#include "zetasql/base/logging.h"
#include "zetasql/base/status.h"
#include "zetasql/base/statusor.h"
#include "zetasql/public/analyzer.h"
#include "zetasql/resolved_ast/resolved_ast.h"

typedef std::pair<std::string, std::string> Edge;

ABSL_FLAG(std::string, output_path, "", "Output path for DAG.");
ABSL_FLAG(std::string, external_required_tables_output_path, "",
          "Output path for external required tables.");

struct table_queries {
  std::string create;
  std::string drop;
  std::vector<std::string> inserts;
  std::vector<std::string> updates;
  std::vector<std::string> others;
};

struct function_queries {
  std::string create;
  std::string drop;
  std::vector<std::string> call;
};

namespace alphasql {

using namespace zetasql;

absl::Status UpdateIdentifierQueriesMapsAndVertices(
    const std::filesystem::path &file_path,
    std::map<std::string, table_queries> &table_queries_map,
    std::map<std::string, function_queries> &function_queries_map,
    std::set<std::string> &vertices) {
  if (file_path.extension() != ".bq" && file_path.extension() != ".sql") {
    return absl::OkStatus();
  }
  std::cout << "Reading " << file_path << std::endl;

  const auto identifier_information_or_status =
      identifier_resolver::GetIdentifierInformation(file_path.string());
  if (!identifier_information_or_status.ok()) {
    return identifier_information_or_status.status();
  }
  const auto identifier_information = identifier_information_or_status.value();

  // Resolve file dependency from table references on DDL.
  for (auto const &table_name :
       identifier_information.table_information.created) {
    const std::string table_string = absl::StrJoin(table_name, ".");
    if (!table_queries_map[table_string].create.empty()) {
      return absl::AlreadyExistsError(
          absl::StrFormat("Table %s already exists!", table_string));
    }
    table_queries_map[table_string].create = file_path;
  }

  // for (auto const& table_name :
  // identifier_information.table_information.dropped) {
  //   const std::string table_string = absl::StrJoin(table_name, ".");
  //   if (!table_queries_map[table_string].drop.empty()) {
  //     return absl::AlreadyExistsError(absl::StrFormat("Table %s dropped
  //     twice!", table_string));
  //   }
  //   table_queries_map[table_string].drop = file_path;
  // }

  // Currently resolve drop statements as reference.
  for (auto const &table_name :
       identifier_information.table_information.dropped) {
    const std::string table_string = absl::StrJoin(table_name, ".");
    if (table_queries_map[table_string].create == file_path) {
      continue;
    }
    table_queries_map[table_string].others.push_back(file_path);
  }

  for (auto const &table_name :
       identifier_information.table_information.referenced) {
    const std::string table_string = absl::StrJoin(table_name, ".");
    if (table_queries_map[table_string].create == file_path) {
      continue;
    }
    table_queries_map[table_string].others.push_back(file_path);
  }

  for (auto const &table_name :
       identifier_information.table_information.inserted) {
    const std::string table_string = absl::StrJoin(table_name, ".");
    table_queries_map[table_string].inserts.push_back(file_path);
  }

  for (auto const &table_name :
       identifier_information.table_information.updated) {
    const std::string table_string = absl::StrJoin(table_name, ".");
    table_queries_map[table_string].updates.push_back(file_path);
  }

  // Resolve file dependency from function calls on definition.
  for (auto const &defined :
       identifier_information.function_information.defined) {
    const std::string function_name = absl::StrJoin(defined, ".");
    if (!function_queries_map[function_name].create.empty()) {
      return absl::AlreadyExistsError(
          absl::StrFormat("Function %s already exists!", function_name));
    }
    function_queries_map[function_name].create = file_path;
  }

  // for (auto const& dropped :
  // identifier_information.function_information.dropped) {
  //   const std::string function_name = absl::StrJoin(dropped, ".");
  //   if (!function_queries_map[function_name].drop.empty()) {
  //     return absl::AlreadyExistsError(absl::StrFormat("Function %s dropped
  //     twice!", function_name));
  //   }
  //   function_queries_map[function_name].drop = file_path;
  // }

  // Currently resolve drop statements as reference.
  for (auto const &dropped :
       identifier_information.function_information.dropped) {
    const std::string function_name = absl::StrJoin(dropped, ".");
    if (function_queries_map[function_name].create == file_path) {
      continue;
    }
    function_queries_map[function_name].call.push_back(file_path);
  }

  for (auto const &called :
       identifier_information.function_information.called) {
    const std::string function_name = absl::StrJoin(called, ".");
    if (function_queries_map[function_name].create == file_path) {
      continue;
    }
    function_queries_map[function_name].call.push_back(file_path);
  }

  // Add the file as a vertice.
  vertices.insert(file_path);

  return absl::OkStatus();
}

void UpdateEdges(std::vector<Edge> &depends_on,
                 std::vector<std::string> dependents, std::string parent) {
  if (!dependents.size() || parent.empty())
    return;
  for (const std::string &dep : dependents) {
    depends_on.push_back(std::make_pair(dep, parent));
  }
}
} // namespace alphasql

struct cycle_detector : public boost::dfs_visitor<> {
  cycle_detector(bool &has_cycle) : _has_cycle(has_cycle) {}

  template <class Edge, class Graph> void back_edge(Edge, Graph &) {
    _has_cycle = true;
  }

protected:
  bool &_has_cycle;
};
