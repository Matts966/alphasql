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

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <filesystem>
#include <fstream>

#include "zetasql/common/errors.h"
#include "zetasql/base/logging.h"
#include "zetasql/parser/ast_node_kind.h"
#include "zetasql/parser/parse_tree.h"
#include "zetasql/parser/parse_tree_errors.h"
#include "zetasql/parser/parser.h"
#include "zetasql/public/analyzer.h"
#include "zetasql/public/language_options.h"
#include "zetasql/public/parse_resume_location.h"
#include "zetasql/public/options.pb.h"
#include "zetasql/resolved_ast/resolved_ast.h"
#include "zetasql/resolved_ast/resolved_node_kind.pb.h"
#include "zetasql/base/case.h"
#include "zetasql/base/map_util.h"
#include "zetasql/base/ret_check.h"
#include "zetasql/base/status.h"
#include "zetasql/base/status_macros.h"
#include "zetasql/base/statusor.h"
#include "alphasql/function_name_resolver.h"


namespace alphasql {

using namespace zetasql::parser;
using namespace zetasql;

const AnalyzerOptions* GetAnalyzerOptions() {
  LanguageOptions language_options;
  language_options.EnableMaximumLanguageFeaturesForDevelopment();
  language_options.SetEnabledLanguageFeatures({FEATURE_V_1_3_ALLOW_DASHES_IN_TABLE_NAME});
  language_options.SetSupportsAllStatementKinds();
  AnalyzerOptions options(language_options);
  options.mutable_language()->EnableMaximumLanguageFeaturesForDevelopment();
  options.CreateDefaultArenasIfNotSet();
  return &options;
}

namespace function_name_resolver {

zetasql_base::StatusOr<function_info> GetFunctionInformation(
    const std::string& sql_file_path, const AnalyzerOptions& analyzer_options) {
  const AnalyzerOptions options = *GetAnalyzerOptions();
  std::unique_ptr<ParserOutput> parser_output;

  std::filesystem::path file_path(sql_file_path);
  std::ifstream file(file_path, std::ios::in);
  std::string sql(std::istreambuf_iterator<char>(file), {});

  ZETASQL_RETURN_IF_ERROR(ParseScript(sql, options.GetParserOptions(),
                              options.error_message_mode(), &parser_output));
  FunctionNameResolver resolver = FunctionNameResolver();
  parser_output->script()->Accept(&resolver, nullptr);
  return resolver.function_information;
}

// Each instance should be used only once.
void FunctionNameResolver::visitASTModelClause(const ASTModelClause* node, void* data) {
  return;
}

void FunctionNameResolver::visitASTTemplatedParameterType(
    const ASTTemplatedParameterType* node, void* data) {
  return;
}

void FunctionNameResolver::visitASTConnectionClause(const ASTConnectionClause* node,
                                        void* data) {
  return;
}

void FunctionNameResolver::visitASTCreateDatabaseStatement(
    const ASTCreateDatabaseStatement* node, void* data) {
  return;
}

void FunctionNameResolver::visitASTFunctionDeclaration(
    const ASTFunctionDeclaration* node, void* data) {
  function_information.defined.insert(node->name()->ToIdentifierVector());
}

void FunctionNameResolver::visitASTTVF(const ASTTVF* node, void* data) {
  function_information.called.insert(node->name()->ToIdentifierVector());
}

void FunctionNameResolver::visitASTCreateFunctionStatement(
    const ASTCreateFunctionStatement* node, void* data) {
  if (node->is_temp()) {
    return;
  }
  node->function_declaration()->Accept(this, data);
  if (node->sql_function_body() != nullptr) {
    node->sql_function_body()->Accept(this, data);
  }
}

void FunctionNameResolver::visitASTCreateTableFunctionStatement(
    const ASTCreateTableFunctionStatement* node, void* data) {
  if (node->is_temp()) {
    return;
  }
  node->function_declaration()->Accept(this, data);
  if (node->query() != nullptr) {
    node->query()->Accept(this, data);
  }
}

void FunctionNameResolver::visitASTCreateEntityStatement(
    const ASTCreateEntityStatement* node, void* data) {
  return;
}

void FunctionNameResolver::visitASTNotNullColumnAttribute(
    const ASTNotNullColumnAttribute* node, void* data) {
  return;
}

void FunctionNameResolver::visitASTHiddenColumnAttribute(
    const ASTHiddenColumnAttribute* node, void* data) {
  return;
}

void FunctionNameResolver::visitASTPrimaryKeyColumnAttribute(
    const ASTPrimaryKeyColumnAttribute* node, void* data) {
  return;
}

void FunctionNameResolver::visitASTColumnDefinition(const ASTColumnDefinition* node,
                                        void* data) {
  return;
}

void FunctionNameResolver::visitASTWithPartitionColumnsClause(
    const ASTWithPartitionColumnsClause* node, void* data) {
  return;
}

void FunctionNameResolver::visitASTCreateExternalTableStatement(
    const ASTCreateExternalTableStatement* node, void* data) {
  return;
}

void FunctionNameResolver::visitASTGrantToClause(const ASTGrantToClause* node, void* data) {
  return;
}

void FunctionNameResolver::visitASTFilterUsingClause(const ASTFilterUsingClause* node,
                                         void* data) {
  return;
}

void FunctionNameResolver::visitASTCreateRowAccessPolicyStatement(
    const ASTCreateRowAccessPolicyStatement* node, void* data) {
  return;
}

void FunctionNameResolver::visitASTExportModelStatement(const ASTExportModelStatement* node,
                                            void* data) {
  return;
}

void FunctionNameResolver::visitASTCallStatement(
    const ASTCallStatement* node, void* data) {
  // print("CALL");
  // node->procedure_name()->Accept(this, data);
  // print("(");
  // UnparseVectorWithSeparator(node->arguments(), data, ",");
  // print(")");

  // Currently procedures are ignored.
  return;
}

void FunctionNameResolver::visitASTDefineTableStatement(
    const ASTDefineTableStatement* node, void* data) {
  return;
}

void FunctionNameResolver::visitASTDescribeStatement(const ASTDescribeStatement* node,
                                         void* data) {
  return;
}

void FunctionNameResolver::visitASTDescriptorColumn(const ASTDescriptorColumn* node,
                                        void* data) {
  return;
}

void FunctionNameResolver::visitASTDescriptorColumnList(const ASTDescriptorColumnList* node,
                                            void* data) {
  return;
}

void FunctionNameResolver::visitASTDescriptor(const ASTDescriptor* node, void* data) {
  return;
}

void FunctionNameResolver::visitASTShowStatement(const ASTShowStatement* node, void* data) {
  return;
}

void FunctionNameResolver::visitASTBeginStatement(
    const ASTBeginStatement* node, void* data) {
  return;
}

void FunctionNameResolver::visitASTTransactionIsolationLevel(
    const ASTTransactionIsolationLevel* node, void* data) {
  return;
}

void FunctionNameResolver::visitASTTransactionReadWriteMode(
    const ASTTransactionReadWriteMode* node, void* data) {
  return;
}

void FunctionNameResolver::visitASTTransactionModeList(const ASTTransactionModeList* node,
                                           void* data) {
  return;
}

void FunctionNameResolver::visitASTSetTransactionStatement(
    const ASTSetTransactionStatement* node, void* data) {
  return;
}

void FunctionNameResolver::visitASTCommitStatement(const ASTCommitStatement* node,
                                       void* data) {
  return;
}

void FunctionNameResolver::visitASTRollbackStatement(const ASTRollbackStatement* node,
                                         void* data) {
  return;
}

void FunctionNameResolver::visitASTStartBatchStatement(const ASTStartBatchStatement* node,
                                           void* data) {
  return;
}

void FunctionNameResolver::visitASTRunBatchStatement(const ASTRunBatchStatement* node,
                                         void* data) {
  return;
}

void FunctionNameResolver::visitASTAbortBatchStatement(const ASTAbortBatchStatement* node,
                                           void* data) {
  return;
}

void FunctionNameResolver::visitASTDropStatement(const ASTDropStatement* node, void* data) {
  return;
}

void FunctionNameResolver::visitASTDropEntityStatement(const ASTDropEntityStatement* node,
                                           void* data) {
  return;
}

void FunctionNameResolver::visitASTDropFunctionStatement(
    const ASTDropFunctionStatement* node, void* data) {
  //TODO: prioritize reference?
  return;
}

void FunctionNameResolver::visitASTDropRowAccessPolicyStatement(
    const ASTDropRowAccessPolicyStatement* node, void* data) {
  return;
}

void FunctionNameResolver::visitASTDropAllRowAccessPoliciesStatement(
    const ASTDropAllRowAccessPoliciesStatement* node, void* data) {
  return;
}

void FunctionNameResolver::visitASTDropMaterializedViewStatement(
    const ASTDropMaterializedViewStatement* node, void* data) {
  return;
}

void FunctionNameResolver::visitASTRenameStatement(const ASTRenameStatement* node,
                                       void* data) {
  return;
}

void FunctionNameResolver::visitASTImportStatement(const ASTImportStatement* node,
                                       void* data) {
  return;
}

void FunctionNameResolver::visitASTModuleStatement(const ASTModuleStatement* node,
                                       void* data) {
  return;
}

}  // namespace function_name_resolver
}  // namespace alphasql
