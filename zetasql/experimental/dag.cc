#include <filesystem>

#include "zetasql/resolved_ast/resolved_ast.h"
#include "zetasql/base/logging.h"
#include "zetasql/base/status.h"
#include "zetasql/public/analyzer.h"
#include "zetasql/analyzer/table_name_resolver.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "boost/graph/graphviz.hpp"
#include "boost/graph/depth_first_search.hpp"
#include "absl/strings/str_join.h"

typedef std::pair<std::string, std::string> Edge;

ABSL_FLAG(std::string, output_path, "",
          "Output path for DAG.");
ABSL_FLAG(std::string, external_required_tables_output_path, "",
          "Output path for external required tables.");

struct table_queries {
  std::vector<std::string> create;
  std::vector<std::string> update;
  std::vector<std::string> insert;
  std::vector<std::string> others;
};

namespace zetasql {
  // Returns <options> if it already has all arenas initialized, or otherwise
  // populates <copy> as a copy for <options>, creates arenas in <copy> and
  // returns it. This avoids unnecessary duplication of AnalyzerOptions, which
  // might be expensive.
  const AnalyzerOptions& GetOptionsWithArenas(
      const AnalyzerOptions* options, std::unique_ptr<AnalyzerOptions>* copy) {
    if (options->AllArenasAreInitialized()) {
        return *options;
    }
    *copy = absl::make_unique<AnalyzerOptions>(*options);
    (*copy)->CreateDefaultArenasIfNotSet();
    return **copy;
  }

  std::map<ResolvedNodeKind, TableNamesSet> ExtractTableNamesFromSQL(const std::string sql,
                                                                     TableNamesSet* table_names) {
    LanguageOptions language_options;
    language_options.EnableMaximumLanguageFeaturesForDevelopment();
    language_options.SetEnabledLanguageFeatures({FEATURE_V_1_3_ALLOW_DASHES_IN_TABLE_NAME});
    language_options.SetSupportsAllStatementKinds();
    AnalyzerOptions options(language_options);
    options.mutable_language()->EnableMaximumLanguageFeaturesForDevelopment();
    options.CreateDefaultArenasIfNotSet();

    return table_name_resolver::GetNodeKindToTableNamesMap(
      sql, options, table_names);
  }

  void UpdateAlreadyInsertedTablesAndTableQueriesMapInternal(const std::string table_string, const std::string file_path,
                                                             std::set<std::string>& already_inserted_tables,
                                                             std::vector<std::string>& files) {
    if (already_inserted_tables.count(table_string)) {
      return;
    }
    already_inserted_tables.insert(table_string);
    files.push_back(file_path);
  }

  void UpdateTableQueriesMapAndVertices(const std::filesystem::path& file_path,
                                        std::map<std::string, table_queries>& table_queries_map,
                                        std::set<std::string>& vertices) {
    if (file_path.extension() != ".bq" && file_path.extension() != ".sql") {
      // std::cout << "not a sql file " << file_path << "!" << std::endl;
      // Skip if not SQL.
      return absl::OkStatus();
    }
    std::cout << "Reading " << file_path << std::endl;
    std::ifstream file(file_path, std::ios::in);
    std::string sql(std::istreambuf_iterator<char>(file), {});
    TableNamesSet table_names;
    std::map<ResolvedNodeKind, TableNamesSet> node_kind_to_table_names =
      ExtractTableNamesFromSQL(sql, &table_names);

    // Check already inserted or not to avoid redundant cycles
    std::set<std::string> already_inserted_tables;

    // Assume only the node kinds below in the map.
    for (auto const& table_name : node_kind_to_table_names[RESOLVED_CREATE_TABLE_STMT]) {
      const std::string table_string = absl::StrJoin(table_name, ".");
      UpdateAlreadyInsertedTablesAndTableQueriesMapInternal(table_string, file_path, already_inserted_tables,
                                                            table_queries_map[table_string].create);
    }
    for (auto const& table_name : node_kind_to_table_names[RESOLVED_CREATE_TABLE_AS_SELECT_STMT]) {
      const std::string table_string = absl::StrJoin(table_name, ".");
      UpdateAlreadyInsertedTablesAndTableQueriesMapInternal(table_string, file_path, already_inserted_tables,
                                                            table_queries_map[table_string].create);
    }
    for (auto const& table_name : node_kind_to_table_names[RESOLVED_UPDATE_STMT]) {
      const std::string table_string = absl::StrJoin(table_name, ".");
      UpdateAlreadyInsertedTablesAndTableQueriesMapInternal(table_string, file_path, already_inserted_tables,
                                                            table_queries_map[table_string].update);
    }
    for (auto const& table_name : node_kind_to_table_names[RESOLVED_INSERT_STMT]) {
      const std::string table_string = absl::StrJoin(table_name, ".");
      UpdateAlreadyInsertedTablesAndTableQueriesMapInternal(table_string, file_path, already_inserted_tables,
                                                            table_queries_map[table_string].insert);
    }

    for (auto const& table_name : table_names) {
      const std::string table_string = absl::StrJoin(table_name, ".");
      UpdateAlreadyInsertedTablesAndTableQueriesMapInternal(table_string, file_path, already_inserted_tables,
                                                            table_queries_map[table_string].others);
    }

    vertices.insert(file_path);

    return;
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

int main(int argc, char* argv[]) {
  const char kUsage[] =
      "Usage: dag --external_required_tables_output_path <filename> --output_path <filename> <directory or file paths of sql...>\n";
  std::vector<char*> args = absl::ParseCommandLine(argc, argv);
  if (argc <= 1) {
    LOG(QFATAL) << kUsage;
  }
  std::vector<char*> remaining_args(args.begin() + 1, args.end());

  std::map<std::string, table_queries> table_queries_map;
  std::set<std::string> vertices;
  std::cout << "Reading paths passed as a command line arguments..." << std::endl;
  std::cout << "Only files that end with .sql or .bq are analyzed." << std::endl;
  for (const auto& path : remaining_args) {
    if (std::filesystem::is_regular_file(path)) {
      std::filesystem::path file_path(path);
      zetasql::UpdateTableQueriesMapAndVertices(file_path, table_queries_map, vertices);
      continue;
    }
    std::filesystem::recursive_directory_iterator file_path(path,
      std::filesystem::directory_options::skip_permission_denied), end;
    std::error_code err;
    for (; file_path != end; file_path.increment(err)) {
      if (err) {
        std::cout << "WARNING: " << err << std::endl;
      }
      zetasql::UpdateTableQueriesMapAndVertices(file_path->path(), table_queries_map, vertices);
    }
  }

  std::vector<Edge> depends_on;
  std::vector<std::string> external_required_tables;
  for (auto const& [table_name, table_queries] : table_queries_map) {
    zetasql::UpdateEdges(depends_on, table_queries.others, {
      table_queries.insert,
      table_queries.update,
      table_queries.create,
    });
    zetasql::UpdateEdges(depends_on, table_queries.insert, {
      table_queries.update,
      table_queries.create,
    });
    zetasql::UpdateEdges(depends_on, table_queries.update, {
      table_queries.create,
    });
    if (table_queries.create.empty()) {
      external_required_tables.push_back(table_name);
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

  const int nedges = depends_on.size();
  // int weights[nedges];
  // std::fill(weights, weights + nedges, 1);

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

  bool has_cycle = false;
  cycle_detector vis(has_cycle);
  depth_first_search(g, visitor(vis));
  if (has_cycle) {
    std::cout << "Warning!!! There are cycles in your dependency graph!!! " << std::endl;
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
  return 0;
}
