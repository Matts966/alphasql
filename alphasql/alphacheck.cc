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
#include <iostream>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <algorithm>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/memory/memory.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/types/optional.h"
#include "google/protobuf/descriptor.h"
#include "zetasql/base/logging.h"
#include "zetasql/public/analyzer.h"
#include "zetasql/public/catalog.h"
#include "zetasql/public/error_helpers.h"
#include "zetasql/public/evaluator.h"
#include "zetasql/public/evaluator_table_iterator.h"
#include "zetasql/public/language_options.h"
#include "zetasql/public/parse_resume_location.h"
#include "zetasql/public/simple_catalog.h"
#include "zetasql/public/type.h"
#include "zetasql/public/value.h"
#include "zetasql/public/templated_sql_function.h"
#include "zetasql/resolved_ast/resolved_ast.h"

#include "alphasql/common_lib.h"
#include "alphasql/json_schema_reader.h"
#include "boost/graph/graphviz.hpp"
#include "zetasql/base/status.h"
#include "zetasql/base/status_macros.h"
#include "zetasql/base/statusor.h"
#include <boost/graph/topological_sort.hpp>

ABSL_FLAG(std::string, json_schema_path, "", "Schema file in JSON format.");

namespace zetasql {

void dropOwnedTable(SimpleCatalog *catalog, const std::string &name) {
  absl::MutexLock l(&catalog->mutex_);

  catalog->tables_.erase(absl::AsciiStrToLower(name));

  for (auto it = catalog->owned_tables_.begin();
       it != catalog->owned_tables_.end();) {
    if (it->get()->Name() == name) {
      it = catalog->owned_tables_.erase(it);
      return;
    } else {
      ++it;
    }
  }
  ZETASQL_CHECK(false) << "No table named " << name;
}

void dropOwnedTableIfExists(SimpleCatalog *catalog, const std::string &name) {
  absl::MutexLock l(&catalog->mutex_);

  catalog->tables_.erase(absl::AsciiStrToLower(name));

  for (auto it = catalog->owned_tables_.begin();
       it != catalog->owned_tables_.end();) {
    if (it->get()->Name() == name) {
      it = catalog->owned_tables_.erase(it);
      return;
    } else {
      ++it;
    }
  }
}

void dropOwnedFunction(SimpleCatalog *catalog,
                       const std::string &full_name_without_group) {
  absl::MutexLock l(&catalog->mutex_);

  catalog->functions_.erase(absl::AsciiStrToLower(full_name_without_group));

  for (auto it = catalog->owned_functions_.begin();
       it != catalog->owned_functions_.end();) {
    if (it->get()->FullName(false /* include_group */) ==
        full_name_without_group) {
      it = catalog->owned_functions_.erase(it);
      return;
    } else {
      ++it;
    }
  }
  ZETASQL_CHECK(false) << "No function named " << full_name_without_group;
}
} // namespace zetasql

namespace alphasql {

using namespace zetasql;

SimpleCatalog *ConstructCatalog(const google::protobuf::DescriptorPool *pool,
                                TypeFactory *type_factory) {
  auto catalog = new zetasql::SimpleCatalog("catalog", type_factory);
  catalog->AddZetaSQLFunctions();
  catalog->SetDescriptorPool(pool);
  const std::string json_schema_path = absl::GetFlag(FLAGS_json_schema_path);
  if (!json_schema_path.empty()) {
    UpdateCatalogFromJSON(json_schema_path, catalog);
  }
  return catalog;
}

std::map<std::vector<std::string>, std::string> procedure_bodies;
std::map<std::vector<std::string>, ASTStatement> procedure_statements;

absl::Status check(const std::string &sql, const ASTStatement *statement,
                   std::vector<std::string> *temp_function_names,
                   std::vector<std::string> *temp_table_names,
                   const AnalyzerOptions &options, SimpleCatalog *catalog) {
  std::unique_ptr<const AnalyzerOutput> output;

  if (statement->node_kind() == AST_BEGIN_END_BLOCK) {
    const ASTBeginEndBlock *stmt = statement->GetAs<ASTBeginEndBlock>();
    for (const auto &body : stmt->statement_list_node()->statement_list()) {
      ZETASQL_RETURN_IF_ERROR(check(sql, body, temp_function_names,
                                    temp_table_names, options, catalog));
    }
    if (stmt->handler_list() == nullptr) {
      return absl::OkStatus();
    }
    for (const ASTExceptionHandler *handler :
         stmt->handler_list()->exception_handler_list()) {
      auto exception_handlers = handler->statement_list()->statement_list();
      for (const auto &handler : exception_handlers) {
        ZETASQL_RETURN_IF_ERROR(check(sql, handler, temp_function_names,
                                      temp_table_names, options, catalog));
      }
    }
    return absl::OkStatus();
  }

  const auto status = AnalyzeStatementFromParserAST(
      *statement, options, sql, catalog, catalog->type_factory(), &output);
  if (!status.ok()) {
    if (status.message().find("Statement not supported") == std::string::npos) {
      return status;
    }
    std::cout << "WARNING: check skipped with the error: " << status << std::endl;
    return absl::OkStatus();
  }

  auto resolved_statement = output->resolved_statement();
  switch (resolved_statement->node_kind()) {
  case RESOLVED_CREATE_TABLE_STMT:
  case RESOLVED_CREATE_TABLE_AS_SELECT_STMT: {
    auto *create_table_stmt =
        resolved_statement->GetAs<ResolvedCreateTableStmt>();
    std::cout << "DDL analyzed, adding table to catalog..." << std::endl;
    std::string table_name = absl::StrJoin(create_table_stmt->name_path(), ".");
    std::unique_ptr<zetasql::SimpleTable> table(
        new zetasql::SimpleTable(table_name));
    for (const auto &column_definition :
         create_table_stmt->column_definition_list()) {
      std::unique_ptr<zetasql::SimpleColumn> column(new SimpleColumn(
          table_name, column_definition->column().name_id().ToString(),
          column_definition->column().type()));
      ZETASQL_RETURN_IF_ERROR(table->AddColumn(column.release(), false));
    }
    dropOwnedTableIfExists(catalog, table_name); // In case it already exists in json schema
    catalog->AddOwnedTable(table.release());
    if (create_table_stmt->create_scope() ==
        ResolvedCreateStatement::CREATE_TEMP) {
      temp_table_names->push_back(table_name);
    }
    break;
  }
  case RESOLVED_CREATE_FUNCTION_STMT: {
    auto *create_function_stmt =
        resolved_statement->GetAs<ResolvedCreateFunctionStmt>();
    std::cout
        << "Create Function Statement analyzed, adding function to catalog..."
        << std::endl;
    std::string function_name =
        absl::StrJoin(create_function_stmt->name_path(), ".");
    if (create_function_stmt->signature().IsTemplated()) {
      TemplatedSQLFunction *function;
      function = new TemplatedSQLFunction(
        create_function_stmt->name_path(),
        create_function_stmt->signature(),
        create_function_stmt->argument_name_list(),
        ParseResumeLocation::FromString(create_function_stmt->code()));
      catalog->AddOwnedFunction(function);
    } else {
      Function *function = new Function(function_name, "group", Function::SCALAR);
      function->AddSignature(create_function_stmt->signature());
      catalog->AddOwnedFunction(function);
    }
    if (create_function_stmt->create_scope() ==
        ResolvedCreateStatement::CREATE_TEMP) {
      temp_function_names->push_back(function_name);
    }
    break;
  }
  // TODO: DROP PROCEDURE Support?
  case RESOLVED_CREATE_PROCEDURE_STMT: {
    auto *create_procedure_stmt =
        resolved_statement->GetAs<ResolvedCreateProcedureStmt>();
    std::cout
        << "Create Procedure Statement analyzed, adding function to catalog..."
        << std::endl;
    const auto result_type = create_procedure_stmt->signature().result_type();
    Procedure *proc = new Procedure(create_procedure_stmt->name_path(), create_procedure_stmt->signature());
    catalog->AddOwnedProcedure(proc);
    procedure_bodies[create_procedure_stmt->name_path()] = create_procedure_stmt->procedure_body();
    const ASTCreateProcedureStatement *stmt = statement->GetAs<ASTCreateProcedureStatement>();
    procedure_statements[create_procedure_stmt->name_path()] = *stmt->body()->statement_list()[0];
    // TODO: TEMP PROCEDURE Support?
    break;
  }
  case RESOLVED_CALL_STMT: {
    auto *call_stmt =
        resolved_statement->GetAs<ResolvedCallStmt>();
    std::cout
        << "Call Procedure Statement analyzed, checking body..."
        << std::endl;
    check(
        procedure_bodies[call_stmt->procedure()->name_path()],
        procedure_statements[call_stmt->procedure()->name_path()],
        &temp_function_names, &temp_table_names, options, catalog
    );
    break;
  }
  case RESOLVED_DROP_STMT: {
    auto *drop_stmt = resolved_statement->GetAs<ResolvedDropStmt>();
    std::cout << "Drop Statement analyzed, dropping table from catalog..."
              << std::endl;
    std::string table_name = absl::StrJoin(drop_stmt->name_path(), ".");
    if (drop_stmt->is_if_exists()) {
      zetasql::dropOwnedTableIfExists(catalog, table_name);
    } else {
      zetasql::dropOwnedTable(catalog, table_name);
    }
    break;
  }
  }

  return absl::OkStatus();
}

// Runs the tool.
absl::Status Run(const std::string &sql_file_path,
                 const AnalyzerOptions &options, SimpleCatalog *catalog) {
  std::filesystem::path file_path(sql_file_path);
  std::cout << "Analyzing " << file_path << std::endl;
  std::ifstream file(file_path, std::ios::in);
  std::string sql(std::istreambuf_iterator<char>(file), {});

  std::vector<std::string> temp_function_names;
  std::vector<std::string> temp_table_names;

  std::unique_ptr<ParserOutput> parser_output;
  ZETASQL_RETURN_IF_ERROR(zetasql::ParseScript(sql, options.GetParserOptions(),
                                                options.error_message_mode(),
                                                &parser_output, file_path));

  auto statements =
      parser_output->script()->statement_list_node()->statement_list();
  for (const ASTStatement *statement : statements) {
    ZETASQL_RETURN_IF_ERROR(check(sql, statement, &temp_function_names,
                                  &temp_table_names, options, catalog));
  }
  /* for (const ASTStatement *statement : statements) { */
  /*   if (statement->node_kind() == AST_BEGIN_END_BLOCK) { */
  /*     const ASTBeginEndBlock *stmt = statement->GetAs<ASTBeginEndBlock>(); */
  /*     auto body = stmt->statement_list_node()->statement_list(); */
  /*     statements.insert(statements.end(), body.begin(), body.end()); */
  /*     for (const ASTExceptionHandler *handler : */
  /*           stmt->handler_list()->exception_handler_list()) { */
  /*       auto exception_handlers =
   * handler->statement_list()->statement_list(); */
  /*       statements.insert(statements.end(), exception_handlers.begin(),
   * exception_handlers.end()); */
  /*     } */
  /*     continue; */
  /*   } */
  /*   ZETASQL_RETURN_IF_ERROR(AnalyzeStatementFromParserAST( */
  /*       *statement, options, sql, catalog, &factory, &output)); */
  /*   auto resolved_statement = output->resolved_statement(); */
  /*   switch (resolved_statement->node_kind()) { */
  /*   case RESOLVED_CREATE_TABLE_STMT: */
  /*   case RESOLVED_CREATE_TABLE_AS_SELECT_STMT: { */
  /*     auto *create_table_stmt = */
  /*         resolved_statement->GetAs<ResolvedCreateTableStmt>(); */
  /*     std::cout << "DDL analyzed, adding table to catalog..." << std::endl;
   */
  /*     std::string table_name = */
  /*         absl::StrJoin(create_table_stmt->name_path(), "."); */
  /*     std::unique_ptr<zetasql::SimpleTable> table( */
  /*         new zetasql::SimpleTable(table_name)); */
  /*     for (const auto &column_definition : */
  /*          create_table_stmt->column_definition_list()) { */
  /*       std::unique_ptr<zetasql::SimpleColumn> column(new SimpleColumn( */
  /*           table_name, column_definition->column().name_id().ToString(), */
  /*           catalog->type_factory()->MakeSimpleType( */
  /*               column_definition->column().type()->kind()))); */
  /*       ZETASQL_RETURN_IF_ERROR(table->AddColumn(column.release(), false));
   */
  /*     } */
  /*     catalog->AddOwnedTable(table.release()); */
  /*     if (create_table_stmt->create_scope() == */
  /*         ResolvedCreateStatement::CREATE_TEMP) { */
  /*       temp_table_names.push_back(table_name); */
  /*     } */
  /*     break; */
  /*   } */
  /*   case RESOLVED_CREATE_FUNCTION_STMT: { */
  /*     auto *create_function_stmt = */
  /*         resolved_statement->GetAs<ResolvedCreateFunctionStmt>(); */
  /*     std::cout */
  /*         << "Create Function Statement analyzed, adding function to
   * catalog..." */
  /*         << std::endl; */
  /*     std::string function_name = */
  /*         absl::StrJoin(create_function_stmt->name_path(), "."); */
  /*     Function *function = */
  /*         new Function(function_name, "group", Function::SCALAR); */
  /*     function->AddSignature(create_function_stmt->signature()); */
  /*     catalog->AddOwnedFunction(function); */
  /*     if (create_function_stmt->create_scope() == */
  /*         ResolvedCreateStatement::CREATE_TEMP) { */
  /*       temp_function_names.push_back(function_name); */
  /*     } */
  /*     break; */
  /*   } */
  /*   case RESOLVED_DROP_STMT: { */
  /*     auto *drop_stmt = resolved_statement->GetAs<ResolvedDropStmt>(); */
  /*     std::cout << "Drop Statement analyzed, dropping table from catalog..."
   */
  /*               << std::endl; */
  /*     std::string table_name = absl::StrJoin(drop_stmt->name_path(), "."); */
  /*     if (drop_stmt->is_if_exists()) { */
  /*       zetasql::dropOwnedTableIfExists(catalog, table_name); */
  /*     } else { */
  /*       zetasql::dropOwnedTable(catalog, table_name); */
  /*     } */
  /*     break; */
  /*   } */
  /*   } */
  /* } */

  for (const auto &table_name : temp_table_names) {
    std::cout << "Removing temporary table " << table_name << std::endl;
    zetasql::dropOwnedTableIfExists(catalog, table_name);
  }

  for (const auto &function_name : temp_function_names) {
    std::cout << "Removing temporary function " << function_name << std::endl;
    zetasql::dropOwnedFunction(catalog, function_name);
  }

  return absl::OkStatus();
}

// TODO: Hide implementation and unify
struct cycle_detector : public boost::dfs_visitor<> {
  cycle_detector(bool &has_cycle) : _has_cycle(has_cycle) {}

  template <class Edge, class Graph> void back_edge(Edge, Graph &) {
    _has_cycle = true;
  }

protected:
  bool &_has_cycle;
};

struct DotVertex {
    std::string name;
    std::string type;
};

bool GetExecutionPlan(const std::string dot_path,
                      std::vector<std::string> &execution_plan) {
  using namespace boost;
  typedef adjacency_list<vecS, vecS, directedS, DotVertex> Graph;
  Graph g;
  dynamic_properties dp(ignore_other_properties);
  dp.property("label", get(&DotVertex::name, g));
  dp.property("type", get(&DotVertex::type, g));
  std::filesystem::path file_path(dot_path);
  std::ifstream file(file_path, std::ios::in);
  if (!boost::read_graphviz(file, g, dp)) {
    return false;
  }

  bool has_cycle = false;
  cycle_detector vis(has_cycle);
  depth_first_search(g, visitor(vis));
  if (has_cycle) {
    std::cerr << "ERROR: cycle detected! [at " << dot_path << ":1:1]"
              << std::endl;
    exit(1);
  }

  std::list<int> result;
  topological_sort(g, std::front_inserter(result));
  property_map<Graph, std::string DotVertex::*>::type names = get(&DotVertex::name, g);
  property_map<Graph, std::string DotVertex::*>::type types = get(&DotVertex::type, g);
  for (int i : result) {
    if (types[i] == "query") {
      execution_plan.push_back(names[i]);
    }
  }
  return true;
}

} // namespace alphasql

int main(int argc, char *argv[]) {
  const char kUsage[] = "Usage: alphacheck [--json_schema_path=<path_to.json>] "
                        "<dependency_graph.dot>\n";
  std::vector<char *> remaining_args = absl::ParseCommandLine(argc, argv);
  if (argc <= 1) {
    std::cerr << kUsage;
    return 1;
  }

  const std::string dot_path =
      absl::StrJoin(remaining_args.begin() + 1, remaining_args.end(), " ");

  if (!std::filesystem::is_regular_file(dot_path) &&
      !std::filesystem::is_fifo(dot_path)) {
    std::cerr << "ERROR: not a file " << dot_path << std::endl;
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
  language_options.SetEnabledLanguageFeatures(
      {zetasql::FEATURE_V_1_3_ALLOW_DASHES_IN_TABLE_NAME});
  language_options.SetSupportsAllStatementKinds();
  zetasql::AnalyzerOptions options(language_options);
  options.mutable_language()->EnableMaximumLanguageFeaturesForDevelopment();
  options.CreateDefaultArenasIfNotSet();

  for (const std::string &sql_file_path : execution_plan) {
    if (!std::filesystem::is_regular_file(sql_file_path)) {
      std::cerr << "ERROR: not a file " << sql_file_path << std::endl;
      return 1;
    }
    absl::Status status = alphasql::Run(sql_file_path, options, catalog);
    if (status.ok()) {
      std::cout << "SUCCESS: analysis finished!" << std::endl;
    } else {
      status = zetasql::UpdateErrorLocationPayloadWithFilenameIfNotPresent(
          status, sql_file_path);
      std::cerr << "ERROR: " << status << std::endl;
      std::cout << "catalog:" << std::endl;
      // For deterministic output
      auto table_names = catalog->table_names();
      std::sort(table_names.begin(), table_names.end());
      for (const std::string &table_name : table_names) {
        std::cout << "\t" << table_name << std::endl;
      }
      return 1;
    }
  }

  std::cout << "Successfully finished type check!" << std::endl;
  return 0;
}
