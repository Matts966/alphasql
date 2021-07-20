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
#include <regex>
#include "absl/flags/flag.h"
#include "alphasql/dag_lib.h"

std::regex DEFAULT_EXCLUDES(".*(.git/.*|.hg/.*|.svn/.*)");

ABSL_FLAG(bool, with_tables, false,
          "Show DAG with tables.");

ABSL_FLAG(bool, with_functions, false,
          "Show DAG with functions.");

ABSL_FLAG(bool, side_effect_first, false,
          "Resolve side effects before references.");

int main(int argc, char* argv[]) {
  const char kUsage[] =
      "Usage: alphadag [--warning_as_error] [--with_tables] [--with_functions] [--side_effect_first] --external_required_tables_output_path <filename> --output_path <filename> <directory or file paths of sql...>\n";
  std::vector<char*> args = absl::ParseCommandLine(argc, argv);
  if (argc <= 1) {
    std::cout << kUsage;
    return 1;
  }
  std::vector<char*> remaining_args(args.begin() + 1, args.end());

  std::map<std::string, table_queries> table_queries_map;
  std::map<std::string, function_queries> function_queries_map;
  std::set<std::string> vertices;
  std::smatch m;
  std::cout << "Reading paths passed as a command line arguments..." << std::endl;
  std::cout << "Only files that end with .sql or .bq are analyzed." << std::endl;
  for (const auto& path : remaining_args) {
    if (std::filesystem::is_regular_file(path)) {
      std::filesystem::path file_path(path);
      std::string path_str = file_path.string();
      if (regex_match(path_str, m, DEFAULT_EXCLUDES)) {
        continue;
      }
      absl::Status status = alphasql::UpdateIdentifierQueriesMapsAndVertices(file_path, table_queries_map,
                                                                             function_queries_map, vertices);
      if (!status.ok()) {
        std::cout << status << std::endl;
        return 1;
      }
      continue;
    }
    std::filesystem::recursive_directory_iterator file_path(path,
      std::filesystem::directory_options::skip_permission_denied), end;
    std::error_code err;
    for (; file_path != end; file_path.increment(err)) {
      std::string path_str = file_path->path().string();
      if (regex_match(path_str, m, DEFAULT_EXCLUDES)) {
        continue;
      }
      if (err) {
        std::cout << "WARNING: " << err << std::endl;
      }
      absl::Status status = alphasql::UpdateIdentifierQueriesMapsAndVertices(file_path->path(), table_queries_map,
                                                                             function_queries_map, vertices);
      if (!status.ok()) {
        std::cout << status << std::endl;
        return 1;
      }
    }
  }

  std::vector<Edge> depends_on;
  std::vector<std::string> external_required_tables;
  std::set<std::string> table_vertices;
  const bool with_tables = absl::GetFlag(FLAGS_with_tables);
  const bool side_effect_first = absl::GetFlag(FLAGS_side_effect_first);
  for (auto& [table_name, table_queries] : table_queries_map) {
    if (side_effect_first) {
      auto inserts_it = table_queries.inserts.begin();
      while (inserts_it != table_queries.inserts.end()) {
        if (*inserts_it == table_queries.create) {
          inserts_it = table_queries.inserts.erase(inserts_it);
        } else {
          alphasql::UpdateEdgesWithoutSelf(depends_on, table_queries.others, *inserts_it);
          ++inserts_it;
        }
      }
      auto updates_it = table_queries.updates.begin();
      while (updates_it != table_queries.updates.end()) {
        if (*updates_it == table_queries.create) {
          updates_it = table_queries.updates.erase(updates_it);
        } else {
          alphasql::UpdateEdgesWithoutSelf(depends_on, table_queries.others, *updates_it);
          ++updates_it;
        }
      }
      if (with_tables) {
        alphasql::UpdateEdgesWithoutSelf(depends_on, table_queries.inserts, table_name);
        alphasql::UpdateEdgesWithoutSelf(depends_on, table_queries.updates, table_name);
        alphasql::UpdateEdgesWithoutSelf(depends_on, table_queries.others, table_name);
        alphasql::UpdateEdgesWithoutSelf(depends_on, {table_name}, table_queries.create);
        table_vertices.insert(table_name);
      } else {
        alphasql::UpdateEdgesWithoutSelf(depends_on, table_queries.inserts, table_queries.create);
        alphasql::UpdateEdgesWithoutSelf(depends_on, table_queries.updates, table_queries.create);
        alphasql::UpdateEdgesWithoutSelf(depends_on, table_queries.others, table_queries.create);
      }
    } else {
      if (with_tables) {
        alphasql::UpdateEdgesWithoutSelf(depends_on, table_queries.others, table_name);
        alphasql::UpdateEdgesWithoutSelf(depends_on, {table_name}, table_queries.create);
        table_vertices.insert(table_name);
      } else {
        alphasql::UpdateEdgesWithoutSelf(depends_on, table_queries.others, table_queries.create);
      }
    }
    if (table_queries.create.empty()) {
      external_required_tables.push_back(table_name);
    }
  }

  const bool with_functions = absl::GetFlag(FLAGS_with_functions);
  std::set<std::string> function_vertices;
  for (auto const& [function_name, function_queries] : function_queries_map) {
    if (with_functions && !function_queries.create.empty()) { // Skip default functions
      alphasql::UpdateEdgesWithoutSelf(depends_on, function_queries.call, function_name);
      alphasql::UpdateEdgesWithoutSelf(depends_on, {function_name}, function_queries.create);
      function_vertices.insert(function_name);
    } else {
      alphasql::UpdateEdgesWithoutSelf(depends_on, function_queries.call, function_queries.create);
    }
  }

  const int nedges = depends_on.size();

  using namespace boost;

  struct vertex_info_t {
    std::string label;
    std::string shape;
    std::string type;
  };
  typedef adjacency_list<vecS, vecS, directedS, vertex_info_t> Graph;
  Graph g(vertices.size() + table_vertices.size() + function_vertices.size());

  std::map<std::string, Graph::vertex_descriptor> indexes;
  // fills the property 'vertex_name_t' of the vertices
  int i = 0;
  for (const auto& vertice : vertices) {
    g[i].label = vertice; // set the property of a vertex
    g[i].type = "query";
    indexes[vertice] = vertex(i, g);     // retrives the associated vertex descriptor
    ++i;
  }
  for (const auto& vertice : table_vertices) {
    g[i].label = vertice; // set the property of a vertex
    g[i].type = "table";
    g[i].shape = "box";
    indexes[vertice] = vertex(i, g);     // retrives the associated vertex descriptor
    ++i;
  }
  for (const auto& vertice : function_vertices) {
    g[i].label = vertice; // set the property of a vertex
    g[i].type = "function";
    g[i].shape = "cds";
    indexes[vertice] = vertex(i, g);     // retrives the associated vertex descriptor
    ++i;
  }

  // adds the edges
  for (int i = 0; i < nedges; i++) {
    // Skip duplicates
    if (edge(indexes[depends_on[i].second], indexes[depends_on[i].first], g).second) {
      continue;
    }
    add_edge(indexes[depends_on[i].second], indexes[depends_on[i].first], g);
  }

  boost::dynamic_properties dp;
  dp.property("shape", get(&vertex_info_t::shape, g));
  dp.property("type", get(&vertex_info_t::type, g));
  dp.property("label", get(&vertex_info_t::label, g));
  dp.property("node_id", get(boost::vertex_index, g));
  const std::string output_path = absl::GetFlag(FLAGS_output_path);
  if (output_path.empty()) {
    write_graphviz_dp(std::cout, g, dp);
  } else {
    if (std::filesystem::is_regular_file(output_path) || std::filesystem::is_fifo(output_path) || !std::filesystem::exists(output_path)) {
      std::filesystem::path parent = std::filesystem::path(output_path).parent_path();
      if (!std::filesystem::is_directory(parent)) {
        std::filesystem::create_directories(parent);
      }
      std::ofstream out(output_path);
      write_graphviz_dp(out, g, dp);
    } else {
      std::cout << "output_path is not a file!" << std::endl;
      return 1;
    }
  }

  const std::string external_required_tables_output_path = absl::GetFlag(FLAGS_external_required_tables_output_path);
  if (external_required_tables_output_path.empty()) {
    std::cout << "EXTERNAL REQUIRED TABLES:" << std::endl;
    for (const auto& required_table : external_required_tables) {
      std::cout << required_table << std::endl;
    }
  } else {
    if (std::filesystem::is_regular_file(external_required_tables_output_path)
        || std::filesystem::is_fifo(external_required_tables_output_path)
        || !std::filesystem::exists(external_required_tables_output_path)) {
      std::filesystem::path parent = std::filesystem::path(external_required_tables_output_path).parent_path();
      if (!std::filesystem::is_directory(parent)) {
        std::filesystem::create_directories(parent);
      }
      std::ofstream out(external_required_tables_output_path);
      for (const auto& required_table : external_required_tables) {
        out << required_table << std::endl;
      }
    } else {
      std::cout << "external_required_tables_output_path is not a file!" << std::endl;
      return 1;
    }
  }

  bool has_cycle = false;
  cycle_detector vis(has_cycle);
  depth_first_search(g, visitor(vis));
  if (has_cycle) {
    std::cout << "Warning!!! There are cycles in your dependency graph!!! " << std::endl;
    const bool warning_as_error = absl::GetFlag(FLAGS_warning_as_error);
    if (warning_as_error) {
      exit(1);
    }
  }

  return 0;
}
