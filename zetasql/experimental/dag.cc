#include <filesystem>

#include "zetasql/resolved_ast/resolved_ast.h"
#include "zetasql/base/logging.h"
#include "zetasql/base/status.h"
#include "zetasql/public/analyzer.h"
#include "zetasql/analyzer/table_name_resolver.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "boost/graph/graphviz.hpp"
#include "absl/strings/str_join.h"

typedef std::pair<std::string, std::string> Edge;

ABSL_FLAG(std::string, output_path, "",
          "Output path for DAG.");

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

  void UpdateTableQueriesMap(const std::filesystem::path& file_path,
                             std::map<std::string, table_queries>& table_queries_map) {
    if (file_path.extension() != ".bq" && file_path.extension() != ".sql") {
      std::cout << "not a sql file " << file_path << "!" << std::endl;
      return;
    }
    std::cout << "reading " << file_path << "..." << std::endl;
    std::ifstream file(file_path, std::ios::in);
    std::string sql(std::istreambuf_iterator<char>(file), {});
    TableNamesSet table_names;
    const std::map<ResolvedNodeKind, TableNamesSet> node_kind_to_table_names =
      ExtractTableNamesFromSQL(sql, &table_names);
    for (auto const& [node_kind, table_names] : node_kind_to_table_names) {
      for (const auto& table_name : table_names) {
        const std::string table_string = absl::StrJoin(table_name, ".");
        switch (node_kind) {
          case RESOLVED_CREATE_TABLE_AS_SELECT_STMT:
            table_queries_map[table_string].create.push_back(file_path);
            break;
          case RESOLVED_UPDATE_STMT:
            table_queries_map[table_string].update.push_back(file_path);
            break;
          case RESOLVED_INSERT_STMT:
            table_queries_map[table_string].insert.push_back(file_path);
            break;
          default:
            std::cout << "unsupported node kind " << ResolvedNodeKindToString(node_kind) << std::endl;
            return;
        }
      }
    }
    for (auto const& table_name : table_names) {
      const std::string table_string = absl::StrJoin(table_name, ".");
      table_queries_map[table_string].others.push_back(file_path);
    }
    return;
  }

  void UpdateEdgesAndVertices(std::vector<Edge>& depends_on, std::set<std::string>& vertices,
                              std::vector<std::string> dependents, std::vector<std::vector<std::string>> parents) {
    if (!dependents.size()) return;
    for (const std::string& dep : dependents) vertices.insert(dep);
    for (const auto& parent : parents) {
      if (!parent.size()) continue;
      for (const std::string& p : parent) {
        vertices.insert(p);
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

int main(int argc, char* argv[]) {
  const char kUsage[] =
      "Usage: dag <directory or file paths of sql...>\n";
  std::vector<char*> args = absl::ParseCommandLine(argc, argv);
  if (argc <= 1) {
    LOG(QFATAL) << kUsage;
  }
  std::vector<char*> remaining_args(args.begin() + 1, args.end());

  std::map<std::string, table_queries> table_queries_map;

  for (const auto& path : remaining_args) {
    if (std::filesystem::is_regular_file(path)) {
      std::filesystem::path file_path(path);
      zetasql::UpdateTableQueriesMap(file_path, table_queries_map);
      continue;
    }
    std::filesystem::recursive_directory_iterator file_path(path,
      std::filesystem::directory_options::skip_permission_denied), end;
    std::error_code err;
    for (; file_path != end; file_path.increment(err)) {
      if (err) {
        std::cout << "WARNING: " << err << std::endl;
      }
      zetasql::UpdateTableQueriesMap(file_path->path(), table_queries_map);
    }
  }

  std::vector<Edge> depends_on;
  std::set<std::string> vertices;
  for (auto const& [_, table_queries] : table_queries_map) {
    zetasql::UpdateEdgesAndVertices(depends_on, vertices, table_queries.others, std::vector<std::vector<std::string>>{
      table_queries.insert,
      table_queries.update,
      table_queries.create,
    });
    zetasql::UpdateEdgesAndVertices(depends_on, vertices, table_queries.insert, std::vector<std::vector<std::string>>{
      table_queries.update,
      table_queries.create,
    });
    zetasql::UpdateEdgesAndVertices(depends_on, vertices, table_queries.update, std::vector<std::vector<std::string>>{
      table_queries.create,
    });
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
    if (edge(indexes[depends_on[i].first], indexes[depends_on[i].second], g).second) {
      continue;
    }
    add_edge(indexes[depends_on[i].first], indexes[depends_on[i].second], g);
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
