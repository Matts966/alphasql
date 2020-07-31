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

#include "zetasql/resolved_ast/resolved_ast.h"
#include "zetasql/base/logging.h"
#include "zetasql/base/status.h"
#include "zetasql/base/statusor.h"
#include "zetasql/public/analyzer.h"
#include "alphasql/identifier_resolver.h"
#include "alphasql/function_name_resolver.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_join.h"
#include "boost/graph/graphviz.hpp"
#include "boost/graph/depth_first_search.hpp"

typedef std::pair<std::string, std::string> Edge;

ABSL_FLAG(std::string, output_path, "",
          "Output path for DAG.");
ABSL_FLAG(std::string, external_required_tables_output_path, "",
          "Output path for external required tables.");

struct table_queries {
  std::vector<std::string> create;
  std::vector<std::string> others;
};

struct function_queries {
  std::vector<std::string> create;
  std::vector<std::string> call;
};

namespace alphasql {

  using namespace zetasql;

  zetasql_base::StatusOr<std::map<ResolvedNodeKind, TableNamesSet>> ExtractTableNamesFromSQL(const std::string& sql_file_path,
                                                                     const AnalyzerOptions& analyzer_options,
                                                                     TableNamesSet* table_names) {
    std::unique_ptr<AnalyzerOptions> copy;
    const AnalyzerOptions& options = GetAnalyzerOptions();
    return identifier_resolver::GetNodeKindToTableNamesMap(
      sql_file_path, options, table_names);
  }

  absl::Status UpdateIdentifierQueriesMapsAndVertices(const std::filesystem::path& file_path,
                                                std::map<std::string, table_queries>& table_queries_map,
                                                std::map<std::string, function_queries>& function_queries_map,
                                                std::set<std::string>& vertices) {
    if (file_path.extension() != ".bq" && file_path.extension() != ".sql") {
      return absl::OkStatus();
    }
    std::cout << "Reading " << file_path << std::endl;
    const AnalyzerOptions options = GetAnalyzerOptions();

    TableNamesSet table_names;
    auto node_kind_to_table_names_or_status = ExtractTableNamesFromSQL(file_path.string(), options, &table_names);
    if (!node_kind_to_table_names_or_status.ok()) {
      return node_kind_to_table_names_or_status.status();
    }
    std::map<ResolvedNodeKind, TableNamesSet> node_kind_to_table_names = node_kind_to_table_names_or_status.value();

    // Resolve file dependency from DML on DDL.
    for (auto const& table_name : node_kind_to_table_names[RESOLVED_CREATE_TABLE_STMT]) {
      const std::string table_string = absl::StrJoin(table_name, ".");
      table_queries_map[table_string].create.push_back(file_path);
    }
    for (auto const& table_name : node_kind_to_table_names[RESOLVED_CREATE_TABLE_AS_SELECT_STMT]) {
      const std::string table_string = absl::StrJoin(table_name, ".");
      table_queries_map[table_string].create.push_back(file_path);
    }

    for (auto const& table_name : table_names) {
      const std::string table_string = absl::StrJoin(table_name, ".");
      table_queries_map[table_string].others.push_back(file_path);
    }

    // Resolve file dependency from SQL files calling functions on the callee.
    auto function_information_or_status = function_name_resolver::GetFunctionInformation(file_path.string(), options);
    if (!function_information_or_status.ok()) {
      return function_information_or_status.status();
    }
    auto function_info = function_information_or_status.value();
    for (auto const& defined : function_info.defined) {
      const std::string function_name = absl::StrJoin(defined, ".");

      std::cout << function_name << " defined in " << file_path << std::endl;

      function_queries_map[function_name].create.push_back(file_path);
    }
    for (auto const& called : function_info.called) {
      const std::string function_name = absl::StrJoin(called, ".");

      std::cout << function_name << " called in " << file_path << std::endl;

      function_queries_map[function_name].call.push_back(file_path);
    }

    // Add the file as a vertice.
    vertices.insert(file_path);

    return absl::OkStatus();
  }

  void UpdateEdges(std::vector<Edge>& depends_on,
                   std::vector<std::string> dependents, std::vector<std::vector<std::string>> parents) {
    if (!dependents.size()) return;
    for (const auto& parent : parents) {
      if (!parent.size()) continue;
      for (const std::string& p : parent) {
        for (const std::string& dep : dependents) {
          if (dep != p) {
            depends_on.push_back(std::make_pair(dep, p));
          }
        }
      }
      return;
    }
  }
}

struct cycle_detector : public boost::dfs_visitor<> {
  cycle_detector( bool& has_cycle)
    : _has_cycle(has_cycle) { }

  template <class Edge, class Graph>
  void back_edge(Edge, Graph&) {
    _has_cycle = true;
  }
protected:
  bool& _has_cycle;
};
