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
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "alphasql/identifier_resolver.h"
#include "alphasql/table_name_resolver.h"
#include "zetasql/base/case.h"
#include "zetasql/base/logging.h"
#include "zetasql/base/map_util.h"
#include "zetasql/base/ret_check.h"
#include "zetasql/base/status.h"
#include "zetasql/base/status_macros.h"
#include "zetasql/base/statusor.h"
#include "zetasql/common/errors.h"
#include "zetasql/parser/ast_node_kind.h"
#include "zetasql/parser/parse_tree.h"
#include "zetasql/parser/parse_tree_errors.h"
#include "zetasql/parser/parser.h"
#include "zetasql/public/analyzer.h"
#include "zetasql/public/language_options.h"
#include "zetasql/public/options.pb.h"
#include "zetasql/public/parse_resume_location.h"
#include "zetasql/resolved_ast/resolved_ast.h"
#include "zetasql/resolved_ast/resolved_node_kind.pb.h"

ABSL_FLAG(bool, warning_as_error, false, "Raise error when emitting warning.");

namespace alphasql {

using namespace zetasql::parser;
using namespace zetasql;

const AnalyzerOptions GetAnalyzerOptions() {
  LanguageOptions language_options;
  language_options.EnableMaximumLanguageFeaturesForDevelopment();
  language_options.SetEnabledLanguageFeatures(
      {FEATURE_V_1_3_ALLOW_DASHES_IN_TABLE_NAME});
  language_options.SetSupportsAllStatementKinds();
  AnalyzerOptions options(language_options);
  options.mutable_language()->EnableMaximumLanguageFeaturesForDevelopment();
  options.CreateDefaultArenasIfNotSet();
  return options;
}

namespace identifier_resolver {

zetasql_base::StatusOr<identifier_info>
GetIdentifierInformation(const std::string &sql_file_path) {
  const AnalyzerOptions options = GetAnalyzerOptions();
  std::unique_ptr<ParserOutput> parser_output;

  std::filesystem::path file_path(sql_file_path);
  std::ifstream file(file_path, std::ios::in);
  std::string sql(std::istreambuf_iterator<char>(file), {});

  ZETASQL_RETURN_IF_ERROR(zetasql::ParseScript(sql, options.GetParserOptions(),
                                                options.error_message_mode(),
                                                &parser_output, file_path));

  IdentifierResolver resolver = IdentifierResolver();
  parser_output->script()->Accept(&resolver, nullptr);
  table_name_resolver::GetTables(
      sql_file_path, options,
      &resolver.identifier_information.table_information.referenced);

  // Filter temporary tables from referenced tables because they are local.
  for (const auto &temporary_table : resolver.temporary_tables) {
    auto referenced_it =
        resolver.identifier_information.table_information.referenced.begin();
    while (referenced_it !=
           resolver.identifier_information.table_information.referenced.end()) {
      if (absl::StrJoin(*referenced_it, ".") == temporary_table) {
        referenced_it =
            resolver.identifier_information.table_information.referenced.erase(
                referenced_it);
      } else {
        ++referenced_it;
      }
    }
  }

  return resolver.identifier_information;
}

void IdentifierResolver::visitASTDropStatement(const ASTDropStatement *node,
                                               void *data) {
  if (node->schema_object_kind() == SchemaObjectKind::kTable) {
    identifier_information.table_information.dropped.insert(
        node->name()->ToIdentifierVector());
  }
  visitASTChildren(node, data);
}

void IdentifierResolver::visitASTCreateTableStatement(
    const ASTCreateTableStatement *node, void *data) {
  const auto &name_vector = node->name()->ToIdentifierVector();
  if (node->scope() == ASTCreateStatement::TEMPORARY) {
    const std::string path_str = absl::StrJoin(name_vector, ".");
    temporary_tables.insert(path_str);
    visitASTChildren(node, data);
    return;
  }

  identifier_information.table_information.created.insert(name_vector);
  visitASTChildren(node, data);
}

// TODO:(Matts966) Check if this node is callee or caller and implement
// correctly void IdentifierResolver::visitASTTVF(const ASTTVF* node, void*
// data) {
//   function_information.called.insert(node->name()->ToIdentifierVector());
// }

// Check INSERT and UPDATE statement to emit warnings for side effects.
void IdentifierResolver::visitASTInsertStatement(const ASTInsertStatement *node,
                                                 void *data) {
  const auto status_or_path = node->GetTargetPathForNonNested();
  if (!status_or_path.ok()) {
    std::cerr << "Path expression can't be extracted" << std::endl;
    std::cerr << status_or_path.status() << std::endl;
    visitASTChildren(node, data);
    return;
  }

  const auto path = status_or_path.value()->ToIdentifierVector();
  identifier_information.table_information.inserted.insert(path);

  const std::string path_str = absl::StrJoin(path, ".");
  for (const auto &created_table :
       identifier_information.table_information.created) {
    if (absl::StrJoin(created_table, ".") == path_str) {
      visitASTChildren(node, data);
      return;
    }
  }
  std::cout << "Warning!!! the target of INSERT statement " << path_str
            << " is not created in the same script!!!" << std::endl;
  std::cout << "This script is not idempotent. See "
               "https://github.com/Matts966/alphasql/issues/"
               "5#issuecomment-735209829 for more details."
            << std::endl;
  const bool warning_as_error = absl::GetFlag(FLAGS_warning_as_error);
  if (warning_as_error) {
    exit(1);
  }
  visitASTChildren(node, data);
}

void IdentifierResolver::visitASTUpdateStatement(const ASTUpdateStatement *node,
                                                 void *data) {
  const auto status_or_path = node->GetTargetPathForNonNested();
  if (!status_or_path.ok()) {
    std::cerr << "Path expression can't be extracted!" << std::endl;
    std::cerr << status_or_path.status() << std::endl;
    visitASTChildren(node, data);
    return;
  }

  const auto path = status_or_path.value()->ToIdentifierVector();
  identifier_information.table_information.updated.insert(path);

  const std::string path_str = absl::StrJoin(path, ".");
  for (const auto &created_table :
       identifier_information.table_information.created) {
    if (absl::StrJoin(created_table, ".") == path_str) {
      visitASTChildren(node, data);
      return;
    }
  }
  std::cout << "Warning!!! the target of UPDATE statement " << path_str
            << " is not created in the same script!!!" << std::endl;
  std::cout << "This script is not idempotent. See "
               "https://github.com/Matts966/alphasql/issues/"
               "5#issuecomment-735209829 for more details."
            << std::endl;
  const bool warning_as_error = absl::GetFlag(FLAGS_warning_as_error);
  if (warning_as_error) {
    exit(1);
  }
  visitASTChildren(node, data);
}

void IdentifierResolver::visitASTDropFunctionStatement(
    const ASTDropFunctionStatement *node, void *data) {
  // if (node->is_if_exists()) {}
  identifier_information.function_information.dropped.insert(
      node->name()->ToIdentifierVector());
  visitASTChildren(node, data);
}

void IdentifierResolver::visitASTFunctionCall(const ASTFunctionCall *node,
                                              void *data) {
  identifier_information.function_information.called.insert(
      node->function()->ToIdentifierVector());
  visitASTChildren(node, data);
}

void IdentifierResolver::visitASTFunctionDeclaration(
    const ASTFunctionDeclaration *node, void *data) {
  identifier_information.function_information.defined.insert(
      node->name()->ToIdentifierVector());
}

void IdentifierResolver::visitASTCreateFunctionStatement(
    const ASTCreateFunctionStatement *node, void *data) {
  if (node->is_temp()) {
    return;
  }
  node->function_declaration()->Accept(this, data);
  if (node->sql_function_body() != nullptr) {
    node->sql_function_body()->Accept(this, data);
  }
}

void IdentifierResolver::visitASTCreateTableFunctionStatement(
    const ASTCreateTableFunctionStatement *node, void *data) {
  if (node->is_temp()) {
    return;
  }
  node->function_declaration()->Accept(this, data);
  if (node->query() != nullptr) {
    node->query()->Accept(this, data);
  }
}

void IdentifierResolver::visitASTCallStatement(const ASTCallStatement *node,
                                               void *data) {
  node->ChildrenAccept(this, data);
  // print("CALL");
  // node->procedure_name()->Accept(this, data);
  // print("(");
  // UnparseVectorWithSeparator(node->arguments(), data, ",");
  // print(")");

  // Currently procedures are ignored.
  return;
}

} // namespace identifier_resolver
} // namespace alphasql
