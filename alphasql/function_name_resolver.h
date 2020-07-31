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

#ifndef ALPHASQL_FUNCTION_NAME_RESOLVER_H_
#define ALPHASQL_FUNCTION_NAME_RESOLVER_H_

#include <string>
#include <filesystem>

#include "zetasql/base/logging.h"
#include "zetasql/parser/parse_tree.h"
#include "zetasql/parser/parse_tree_visitor.h"
#include "zetasql/public/analyzer.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace alphasql {

using namespace zetasql::parser;
using namespace zetasql;

const AnalyzerOptions GetAnalyzerOptions();

namespace function_name_resolver {

struct function_info {
  std::set<std::vector<std::string>> called;
  std::set<std::vector<std::string>> defined;
};

zetasql_base::StatusOr<function_info> GetFunctionInformation(
    const std::string& sql_file_path, const AnalyzerOptions& analyzer_options);

class FunctionNameResolver : public DefaultParseTreeVisitor {
 public:
  explicit FunctionNameResolver() {}
  FunctionNameResolver(const FunctionNameResolver&) = delete;
  FunctionNameResolver& operator=(const FunctionNameResolver&) = delete;
  ~FunctionNameResolver() override {}

  function_info function_information;

  void defaultVisit(const ASTNode* node, void* data) override {
    visitASTChildren(node, data);
  }

  void visitASTChildren(const ASTNode* node, void* data) {
    node->ChildrenAccept(this, data);
  }

  void visit(const ASTNode* node, void* data) override {
    visitASTChildren(node, data);
  }

  // Visitor implementation.
  void visitASTModelClause(const ASTModelClause* node, void* data) override;
  void visitASTTemplatedParameterType(
      const ASTTemplatedParameterType* node, void* data) override;
  void visitASTConnectionClause(const ASTConnectionClause* node,
                                void* data) override;
  void visitASTCreateDatabaseStatement(const ASTCreateDatabaseStatement* node,
                                       void* data) override; 
  void visitASTFunctionDeclaration(
      const ASTFunctionDeclaration* node, void* data) override;
  void visitASTTVF(
      const ASTTVF* node, void* data) override;
  void visitASTCreateFunctionStatement(const ASTCreateFunctionStatement* node,
                                       void* data) override;
  void visitASTCreateTableFunctionStatement(
      const ASTCreateTableFunctionStatement* node, void* data) override;
  void visitASTCreateEntityStatement(const ASTCreateEntityStatement* node,
                                     void* data) override;
  void visitASTNotNullColumnAttribute(
      const ASTNotNullColumnAttribute* node, void* data) override;
  void visitASTHiddenColumnAttribute(
      const ASTHiddenColumnAttribute* node, void* data) override;
  void visitASTPrimaryKeyColumnAttribute(
      const ASTPrimaryKeyColumnAttribute* node, void* data) override;
  void visitASTColumnDefinition(const ASTColumnDefinition* node,
                                void* data) override;
  void visitASTWithPartitionColumnsClause(
      const ASTWithPartitionColumnsClause* node, void* data) override;
  void visitASTCreateExternalTableStatement(
      const ASTCreateExternalTableStatement* node, void* data) override;
  void visitASTGrantToClause(const ASTGrantToClause* node, void* data) override;
  void visitASTFilterUsingClause(const ASTFilterUsingClause* node,
                                 void* data) override;
  void visitASTCreateRowAccessPolicyStatement(
      const ASTCreateRowAccessPolicyStatement* node, void* data) override;
  void visitASTExportModelStatement(const ASTExportModelStatement* node,
                                    void* data) override;
  void visitASTCallStatement(const ASTCallStatement* node,
                             void* data) override;
  void visitASTDefineTableStatement(const ASTDefineTableStatement* node,
                                    void* data) override;
  void visitASTDescribeStatement(const ASTDescribeStatement* node,
                                 void* data) override;
  void visitASTDescriptorColumn(const ASTDescriptorColumn* node,
                                void* data) override;
  void visitASTDescriptor(const ASTDescriptor* node, void* data) override;
  void visitASTShowStatement(const ASTShowStatement* node,
                             void* data) override;
  void visitASTBeginStatement(const ASTBeginStatement* node,
                              void* data) override;
  void visitASTTransactionIsolationLevel(
      const ASTTransactionIsolationLevel* node, void* data) override;
  void visitASTTransactionReadWriteMode(const ASTTransactionReadWriteMode* node,
                                        void* data) override;
  void visitASTTransactionModeList(const ASTTransactionModeList* node,
                                   void* data) override;
  void visitASTSetTransactionStatement(const ASTSetTransactionStatement* node,
                                       void* data) override;

  void visitASTCommitStatement(const ASTCommitStatement* node,
                               void* data) override;
  void visitASTRollbackStatement(const ASTRollbackStatement* node,
                                 void* data) override;
  void visitASTStartBatchStatement(const ASTStartBatchStatement* node,
                                   void* data) override;
  void visitASTRunBatchStatement(const ASTRunBatchStatement* node,
                                 void* data) override;
  void visitASTAbortBatchStatement(const ASTAbortBatchStatement* node,
                                   void* data) override;
  void visitASTDropStatement(const ASTDropStatement* node, void* data) override;
  void visitASTDropEntityStatement(const ASTDropEntityStatement* node,
                                   void* data) override;
  void visitASTDropFunctionStatement(
      const ASTDropFunctionStatement* node, void* data) override;
  void visitASTDropRowAccessPolicyStatement(
      const ASTDropRowAccessPolicyStatement* node, void* data) override;
  void visitASTDropAllRowAccessPoliciesStatement(
      const ASTDropAllRowAccessPoliciesStatement* node, void* data) override;
  void visitASTDropMaterializedViewStatement(
      const ASTDropMaterializedViewStatement* node, void* data) override;
  void visitASTRenameStatement(const ASTRenameStatement* node,
                               void* data) override;
  void visitASTImportStatement(const ASTImportStatement* node,
                               void* data) override;
  void visitASTModuleStatement(const ASTModuleStatement* node,
                               void* data) override;
};

}  // namespace alphasql
}  // namespace function_name_resolver

#endif  // ALPHASQL_FUNCTION_NAME_RESOLVER_H_
