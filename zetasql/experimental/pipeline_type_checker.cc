#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <list>
#include <filesystem>

#include "zetasql/base/logging.h"
#include "google/protobuf/descriptor.h"
#include "zetasql/experimental/output_query_result.h"
#include "zetasql/experimental/json_schema_reader.h"
#include "zetasql/public/analyzer.h"
#include "zetasql/public/catalog.h"
#include "zetasql/public/evaluator.h"
#include "zetasql/public/evaluator_table_iterator.h"
#include "zetasql/public/language_options.h"
#include "zetasql/public/simple_catalog.h"
#include "zetasql/public/type.h"
#include "zetasql/public/value.h"
#include "zetasql/public/parse_resume_location.h"
#include "zetasql/resolved_ast/resolved_ast.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/memory/memory.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/types/optional.h"
#include "zetasql/base/source_location.h"
#include "zetasql/base/status.h"
#include "zetasql/base/status_macros.h"
#include "zetasql/base/statusor.h"
#include "boost/graph/graphviz.hpp"
#include <boost/graph/topological_sort.hpp>

ABSL_FLAG(std::string, json_schema_path, "",
          "Schema file in JSON format.");

namespace zetasql {

namespace {

// Constructs a Catalog corresponding to --table_spec.
SimpleCatalog* ConstructCatalog(
    const google::protobuf::DescriptorPool* pool, TypeFactory* type_factory) {
  auto catalog = new SimpleCatalog("catalog", type_factory);
  catalog->AddZetaSQLFunctions();
  catalog->SetDescriptorPool(pool);
  const std::string json_schema_path = absl::GetFlag(FLAGS_json_schema_path);
  if (json_schema_path != "") {
    zetasql::UpdateCatalogFromJSON(json_schema_path, catalog);
  }
  return catalog;
}

// Prints the result of executing a query. Currently requires loading all the
// results into memory to format pretty output.
absl::Status PrintResults(std::unique_ptr<EvaluatorTableIterator> iter) {
  TypeFactory type_factory;

  std::vector<StructField> struct_fields;
  struct_fields.reserve(iter->NumColumns());
  for (int i = 0; i < iter->NumColumns(); ++i) {
    struct_fields.emplace_back(iter->GetColumnName(i), iter->GetColumnType(i));
  }

  const StructType* struct_type;
  ZETASQL_RETURN_IF_ERROR(type_factory.MakeStructType(struct_fields, &struct_type));

  std::vector<Value> rows;
  while (true) {
    if (!iter->NextRow()) {
      ZETASQL_RETURN_IF_ERROR(iter->Status());
      break;
    }

    std::vector<Value> fields;
    fields.reserve(iter->NumColumns());
    for (int i = 0; i < iter->NumColumns(); ++i) {
      fields.push_back(iter->GetValue(i));
    }

    rows.push_back(Value::Struct(struct_type, fields));
  }

  const ArrayType* array_type;
  ZETASQL_RETURN_IF_ERROR(type_factory.MakeArrayType(struct_type, &array_type));

  const Value result = Value::Array(array_type, rows);

  std::vector<std::string> column_names;
  column_names.reserve(iter->NumColumns());
  for (int i = 0; i < iter->NumColumns(); ++i) {
    column_names.push_back(iter->GetColumnName(i));
  }

  std::cout << ToPrettyOutputStyle(result,
                                   /*is_value_table=*/false, column_names)
            << std::endl;

  return absl::OkStatus();
}

// Runs the tool.
absl::Status Run(const std::string& sql, const AnalyzerOptions& options, SimpleCatalog* catalog) {
//   PreparedQuery query(sql, EvaluatorOptions());
//   ZETASQL_RETURN_IF_ERROR(query.Prepare(options, catalog));
//   ZETASQL_ASSIGN_OR_RETURN(std::unique_ptr<EvaluatorTableIterator> iter,
//                            query.Execute());
//   return PrintResults(std::move(iter));

//   ZETASQL_ASSIGN_OR_RETURN(const std::string explain, query.ExplainAfterPrepare());
//   std::cout << explain << std::endl;
//   return absl::OkStatus();

  TypeFactory factory;
  ParseResumeLocation location = ParseResumeLocation::FromStringView(sql);
  bool at_end_of_input = false;
  std::unique_ptr<const AnalyzerOutput> output;

  std::vector<std::string> temp_function_names;

  while (!at_end_of_input) {
    ZETASQL_RETURN_IF_ERROR(AnalyzeNextStatement(&location, options, catalog, &factory, &output, &at_end_of_input));

    auto resolved_statement = output->resolved_statement();

    switch (resolved_statement->node_kind()) {
      case RESOLVED_CREATE_TABLE_STMT:
      case RESOLVED_CREATE_TABLE_AS_SELECT_STMT: {
        auto* create_table_stmt = resolved_statement->GetAs<ResolvedCreateTableStmt>();
        std::cout << "DDL analyzed, adding table to catalog..." << std::endl;
        std::string table_name = absl::StrJoin(create_table_stmt->name_path(), ".");
        std::unique_ptr<zetasql::SimpleTable> table(new zetasql::SimpleTable(table_name));
        for (const auto& column_definition : create_table_stmt->column_definition_list()) {
          std::unique_ptr<zetasql::SimpleColumn> column(new SimpleColumn(table_name, column_definition->column().name_id().ToString(),
            catalog->type_factory()->MakeSimpleType(column_definition->column().type()->kind())));
          ZETASQL_RETURN_IF_ERROR(table->AddColumn(column.release(), false));
        }
        catalog->AddTable(table.release());
        break;
      }
      case RESOLVED_CREATE_FUNCTION_STMT: {
        auto* create_function_stmt = resolved_statement->GetAs<ResolvedCreateFunctionStmt>();
        std::cout << "Create Function Statement analyzed, adding function to catalog..." << std::endl;
        std::string function_name = absl::StrJoin(create_function_stmt->name_path(), ".");
        Function* function = new Function(function_name, "group", Function::SCALAR);
        function->AddSignature(create_function_stmt->signature());
        catalog->AddOwnedFunction(function);
        if (create_function_stmt->create_scope() == ResolvedCreateStatement::CREATE_TEMP) {
          temp_function_names.push_back(function_name);
        }
        break;
      }
    }
  }

  for (const auto& function_name : temp_function_names) {
    std::cout << "Removing temporary function " << function_name << std::endl;
    catalog->RemoveOwnedFunction(function_name);
  }

  return absl::OkStatus();
}

bool GetExecutionPlan(const std::string dot_path, std::vector<std::string>& execution_plan) {
  using namespace boost;
  typedef adjacency_list<vecS, vecS, directedS, property<vertex_name_t, std::string>> Graph;
  Graph g;
  dynamic_properties dp(ignore_other_properties);
  dp.property("label", get(vertex_name, g));
  if (!std::filesystem::is_regular_file(dot_path)) {
    return false;
  }
  std::filesystem::path file_path(dot_path);
  std::ifstream file(file_path, std::ios::in);
  if (!boost::read_graphviz(file, g, dp)) {
    return false;
  }
  std::list<int> result;
  topological_sort(g, std::front_inserter(result));
  property_map<Graph, vertex_name_t>::type names = get(vertex_name, g);
  for (int i : result) {
    execution_plan.push_back(names[i]);
  }
  return true;
}

}  // namespace
}  // namespace zetasql

int main(int argc, char* argv[]) {
  const char kUsage[] =
      "Usage: pipeline_type_checker [--json_schema_path=<path_to.json>] <dependency_graph.dot>\n";
  std::vector<char*> remaining_args = absl::ParseCommandLine(argc, argv);
  if (argc <= 1) {
    LOG(QFATAL) << kUsage;
  }

  const std::string dot_path = absl::StrJoin(remaining_args.begin() + 1,
  remaining_args.end(), " ");
  if (!std::filesystem::is_regular_file(dot_path)) {
    std::cout << "ERROR: not a file " << dot_path << std::endl;
    return 1;
  }

  std::vector<std::string> execution_plan;
  zetasql::GetExecutionPlan(dot_path, execution_plan);

  const google::protobuf::DescriptorPool &pool =
    *google::protobuf::DescriptorPool::generated_pool();
  zetasql::TypeFactory type_factory;
  auto catalog = zetasql::ConstructCatalog(&pool, &type_factory);

  zetasql::LanguageOptions language_options;
  language_options.EnableMaximumLanguageFeaturesForDevelopment();
  language_options.SetEnabledLanguageFeatures({zetasql::FEATURE_V_1_3_ALLOW_DASHES_IN_TABLE_NAME});
  language_options.SetSupportsAllStatementKinds();
  zetasql::AnalyzerOptions options(language_options);
  options.mutable_language()->EnableMaximumLanguageFeaturesForDevelopment();
  options.CreateDefaultArenasIfNotSet();

  for (const std::string& sql_file_path : execution_plan) {
    if (!std::filesystem::is_regular_file(sql_file_path)) {
      std::cout << "ERROR: not a file " << sql_file_path << std::endl;
      return 1;
    }
    std::filesystem::path file_path(sql_file_path);
    std::cout << "analyzing " << sql_file_path << std::endl;
    std::ifstream file(file_path, std::ios::in);
    std::string sql(std::istreambuf_iterator<char>(file), {});
    const absl::Status status = zetasql::Run(sql, options, catalog);
    if (status.ok()) {
      std::cout << "SUCCESS: analysis finished!" << std::endl;
      std::cout << "catalog:" << std::endl;
      for (const std::string& table_name : catalog->table_names()) {
        std::cout << "\t" << table_name << std::endl;
      }
    } else {
      std::cout << "ERROR: " << status << std::endl;
      return 1;
    }
  }
}
