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
#include "alphasql/dag_lib.h"

int main(int argc, char* argv[]) {
  const char kUsage[] =
      "Usage: dag --external_required_tables_output_path <filename> --output_path <filename> <directory or file paths of sql...>\n";
  std::vector<char*> args = absl::ParseCommandLine(argc, argv);
  if (argc <= 1) {
    ZETASQL_LOG(QFATAL) << kUsage;
  }
  std::vector<char*> remaining_args(args.begin() + 1, args.end());

  std::map<std::string, table_queries> table_queries_map;
  std::map<std::string, function_queries> function_queries_map;
  std::set<std::string> vertices;
  std::cout << "Reading paths passed as a command line arguments..." << std::endl;
  std::cout << "Only files that end with .sql or .bq are analyzed." << std::endl;
  for (const auto& path : remaining_args) {
    if (std::filesystem::is_regular_file(path)) {
      std::filesystem::path file_path(path);
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
  for (auto const& [table_name, table_queries] : table_queries_map) {
    alphasql::UpdateEdges(depends_on, table_queries.others, {
      table_queries.create,
    });
    if (table_queries.create.empty()) {
      external_required_tables.push_back(table_name);
    }
  }

  for (auto const& [_, function_queries] : function_queries_map) {
    alphasql::UpdateEdges(depends_on, function_queries.call, {
      function_queries.create,
    });
  }

  const int nedges = depends_on.size();

  using namespace boost;

  typedef adjacency_list<vecS, vecS, directedS, property<vertex_name_t, std::string>> Graph;
  Graph g(vertices.size());

  std::map<std::string, Graph::vertex_descriptor> indexes;
  // fills the property 'vertex_name_t' of the vertices
  int i = 0;
  for (const auto& vertice : vertices) {
    put(vertex_name_t(), g, i, vertice); // set the property of a vertex
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

  const std::string output_path = absl::GetFlag(FLAGS_output_path);
  if (output_path == "") {
    write_graphviz(std::cout, g, make_label_writer(get(vertex_name, g)));
  } else {
    if (std::filesystem::is_regular_file(output_path) || !std::filesystem::exists(output_path)) {
      std::ofstream out(output_path);
      write_graphviz(out, g, make_label_writer(get(vertex_name, g)));
    } else {
      std::cout << "output_path is not a regular_file!" << std::endl;
      return 1;
    }
  }

  if (!external_required_tables.empty()) {
    const std::string external_required_tables_output_path = absl::GetFlag(FLAGS_external_required_tables_output_path);
    if (external_required_tables_output_path == "") {
      std::cout << "EXTERNAL REQUIRED TABLES:" << std::endl;
      for (const auto& required_table : external_required_tables) {
        std::cout << required_table << std::endl;
      }
    } else {
      if (std::filesystem::is_regular_file(external_required_tables_output_path)
          || !std::filesystem::exists(external_required_tables_output_path)) {
        std::ofstream out(external_required_tables_output_path);
        for (const auto& required_table : external_required_tables) {
          out << required_table << std::endl;
        }
      } else {
        std::cout << "external_required_tables_output_path is not a regular_file!" << std::endl;
        return 1;
      }
    }
  }

  bool has_cycle = false;
  cycle_detector vis(has_cycle);
  depth_first_search(g, visitor(vis));
  if (has_cycle) {
    std::cout << "Warning!!! There are cycles in your dependency graph!!! " << std::endl;
  }

  return 0;
}
