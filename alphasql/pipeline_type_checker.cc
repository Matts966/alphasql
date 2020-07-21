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

#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <list>
#include <filesystem>

#include "zetasql/base/logging.h"
#include "google/protobuf/descriptor.h"
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
#include "alphasql/json_schema_reader.h"
#include "boost/graph/graphviz.hpp"
#include <boost/graph/topological_sort.hpp>

ABSL_FLAG(std::string, json_schema_path, "",
          "Schema file in JSON format.");

namespace zetasql {

void dropOwnedTable(SimpleCatalog* catalog, const std::string& name) {
  absl::MutexLock l(&catalog->mutex_);

  catalog->tables_.erase(absl::AsciiStrToLower(name));

  for (auto it = catalog->owned_tables_.begin(); it != catalog->owned_tables_.end();) {
    if (it->get()->Name() == name) {
      it = catalog->owned_tables_.erase(it);
      return;
    } else {
      ++it;
    }
  }
  CHECK(false) << "No table named " << name;
}

void dropOwnedTableIfExists(SimpleCatalog* catalog, const std::string& name) {
  absl::MutexLock l(&catalog->mutex_);

  catalog->tables_.erase(absl::AsciiStrToLower(name));

  for (auto it = catalog->owned_tables_.begin(); it != catalog->owned_tables_.end();) {
    if (it->get()->Name() == name) {
      it = catalog->owned_tables_.erase(it);
      return;
    } else {
      ++it;
    }
  }
}

void dropOwnedFunction(SimpleCatalog* catalog, const std::string& full_name_without_group) {
  absl::MutexLock l(&catalog->mutex_);

  catalog->functions_.erase(absl::AsciiStrToLower(full_name_without_group));

  for (auto it = catalog->owned_functions_.begin(); it != catalog->owned_functions_.end();) {
    if (it->get()->FullName(false /* include_group */) == full_name_without_group) {
      it = catalog->owned_functions_.erase(it);
      return;
    } else {
      ++it;
    }
  }
  CHECK(false) << "No function named " << full_name_without_group;
}
}


namespace alphasql {

using namespace zetasql;

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

// Runs the tool.
absl::Status Run(const std::string& sql_file_path, const AnalyzerOptions& options, SimpleCatalog* catalog) {
  std::filesystem::path file_path(sql_file_path);
  std::cout << "Analyzing " << file_path << std::endl;
  std::ifstream file(file_path, std::ios::in);
  std::string sql(std::istreambuf_iterator<char>(file), {});

  TypeFactory factory;
  ParseResumeLocation location = ParseResumeLocation::FromStringView(sql_file_path, sql);
  bool at_end_of_input = false;
  std::unique_ptr<const AnalyzerOutput> output;

  std::vector<std::string> temp_function_names;
  std::vector<std::string> temp_table_names;

  while (!at_end_of_input) {
    ZETASQL_RETURN_IF_ERROR(AnalyzeNextStatement(&location, options, catalog, &factory, &output, &at_end_of_input));
    auto resolved_statement = output->resolved_statement();
    switch (resolved_statement->node_kind()) {
      case RESOLVED_CREATE_TABLE_STMT:
      case RESOLVED_CREATE_TABLE_AS_SELECT_STMT: {
        auto* create_table_stmt = resolved_statement->GetAs<ResolvedCreateTableStmt>();
        std::cout << "DDL analyzed, adding table to catalog..." << std::endl;
        std::string table_name = absl::StrJoin(create_table_stmt->name_path(), ".");
        // TODO(Matts966): raise error for duplicated table_names.
        std::unique_ptr<zetasql::SimpleTable> table(new zetasql::SimpleTable(table_name));
        for (const auto& column_definition : create_table_stmt->column_definition_list()) {
          std::unique_ptr<zetasql::SimpleColumn> column(new SimpleColumn(table_name, column_definition->column().name_id().ToString(),
            catalog->type_factory()->MakeSimpleType(column_definition->column().type()->kind())));
          ZETASQL_RETURN_IF_ERROR(table->AddColumn(column.release(), false));
        }
        catalog->AddOwnedTable(table.release());
        if (create_table_stmt->create_scope() == ResolvedCreateStatement::CREATE_TEMP) {
          temp_table_names.push_back(table_name);
        }
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
      case RESOLVED_DROP_STMT: {
        auto* drop_stmt = resolved_statement->GetAs<ResolvedDropStmt>();
        std::cout << "Drop Statement analyzed, dropping table from catalog..." << std::endl;
        std::string table_name = absl::StrJoin(drop_stmt->name_path(), ".");
        if (drop_stmt->is_if_exists()) {
          zetasql::dropOwnedTableIfExists(catalog, table_name);
        } else {
          zetasql::dropOwnedTable(catalog, table_name);
        }
        break;
      }
    }
  }

  for (const auto& table_name : temp_table_names) {
    std::cout << "Removing temporary table " << table_name << std::endl;
    zetasql::dropOwnedTableIfExists(catalog, table_name);
  }

  for (const auto& function_name : temp_function_names) {
    std::cout << "Removing temporary function " << function_name << std::endl;
    zetasql::dropOwnedFunction(catalog, function_name);
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

}  // namespace alphasql

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
  alphasql::GetExecutionPlan(dot_path, execution_plan);

  const google::protobuf::DescriptorPool &pool =
    *google::protobuf::DescriptorPool::generated_pool();
  zetasql::TypeFactory type_factory;
  auto catalog = alphasql::ConstructCatalog(&pool, &type_factory);

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
    const absl::Status status = alphasql::Run(sql_file_path, options, catalog);
    if (status.ok()) {
      std::cout << "SUCCESS: analysis finished!" << std::endl;
    } else {
      std::cout << "ERROR: " << status << std::endl;
      std::cout << "catalog:" << std::endl;
      for (const std::string& table_name : catalog->table_names()) {
        std::cout << "\t" << table_name << std::endl;
      }
      return 1;
    }
  }

  std::cout << "Successfully finished type check!" << std::endl;
  return 0;
}
