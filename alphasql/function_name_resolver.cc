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


namespace alphasql {

using namespace parser;
using namespace zetasql;

namespace function_name_resolver {

// Each instance should be used only once.
void FunctionNameResolver::visitASTHintedStatement(const ASTHintedStatement* node,
                                       void* data) {
  visitASTChildren(node, data);
}

void FunctionNameResolver::visitASTExplainStatement(const ASTExplainStatement* node,
                                        void* data) {
  node->statement()->Accept(this, data);
}

void FunctionNameResolver::visitASTQueryStatement(const ASTQueryStatement* node,
                                      void* data) {
  visitASTQuery(node->query(), data);
}

void Unparser::visitASTModelClause(const ASTModelClause* node, void* data) {
  return;
}

void Unparser::visitASTFunctionParameters(
    const ASTFunctionParameters* node, void* data) {
  visitASTChildren(node, data);
}

void Unparser::visitASTTemplatedParameterType(
    const ASTTemplatedParameterType* node, void* data) {
  return;
}

void Unparser::visitASTFunctionParameters(
    const ASTFunctionParameters* node, void* data) {
  visitASTChildren(node, data);
}

void Unparser::visitASTConnectionClause(const ASTConnectionClause* node,
                                        void* data) {
  return;
}

void Unparser::visitASTTVFSchema(const ASTTVFSchema* node, void* data) {
  visitASTChildren(node, data);
}

void Unparser::visitASTCreateDatabaseStatement(
    const ASTCreateDatabaseStatement* node, void* data) {
  return;
}

void Unparser::visitASTCreateTableStatement(
    const ASTCreateTableStatement* node, void* data) {
  if (node->query() != nullptr) {
    node->query()->Accept(this, data);
  }
}

void FunctionNameResolver::visitASTFunctionDeclaration(
    const ASTFunctionDeclaration* node, void* data) {
  if (node->is_temp()) {
    return;
  }
  function_information.defined.push_back(node->name()->ToIdentifierVector);
}

void FunctionNameResolver::visitASTSqlFunctionBody(
    const ASTSqlFunctionBody* node, void* data) {
  node->expression()->Accept(this, data);
}

void FunctionNameResolver::visitASTTableClause(const ASTTableClause* node, void* data) {
  if (node->tvf() != nullptr) {
    node->tvf()->Accept(this, data);
  }
}

void FunctionNameResolver::visitASTTVF(const ASTTVF* node, void* data) {
  function_information.called.push_back(node->name()->ToIdentifierVector);
}

void FunctionNameResolver::visitASTTVFArgument(const ASTTVFArgument* node, void* data) {
  if (node->expr() != nullptr) {
    node->expr()->Accept(this, data);
  }
}

void FunctionNameResolver::visitASTTVFSchemaColumn(const ASTTVFSchemaColumn* node,
                                       void* data) {
  visitASTChildren(node, data);
}

void FunctionNameResolver::visitASTCreateConstantStatement(
    const ASTCreateConstantStatement* node, void* data) {
  node->expr()->Accept(this, data);
}

void FunctionNameResolver::visitASTCreateFunctionStatement(
    const ASTCreateFunctionStatement* node, void* data) {
  node->function_declaration()->Accept(this, data);
  if (node->sql_function_body() != nullptr) {
    node->sql_function_body()->Accept(this, data);
  }
}

void FunctionNameResolver::visitASTCreateTableFunctionStatement(
    const ASTCreateTableFunctionStatement* node, void* data) {
  node->function_declaration()->Accept(this, data);
  if (node->query() != nullptr) {
    node->query()->Accept(this, data);
  }
}

void FunctionNameResolver::visitASTCreateEntityStatement(
    const ASTCreateEntityStatement* node, void* data) {
  return;
}

void FunctionNameResolver::visitASTAlterEntityStatement(const ASTAlterEntityStatement* node,
                                            void* data) {
  VisitAlterStatementBase(node, data);
}

void FunctionNameResolver::visitASTCreateModelStatement(const ASTCreateModelStatement* node,
                                            void* data) {
  if (node->transform_clause() != nullptr) {
    node->transform_clause()->Accept(this, data);
  }
  if (node->query() != nullptr) {
    node->query()->Accept(this, data);
  }
}

void FunctionNameResolver::visitASTTableElementList(const ASTTableElementList* node,
                                        void* data) {
  visitASTChildren(node, data);
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

void FunctionNameResolver::visitASTForeignKeyColumnAttribute(
    const ASTForeignKeyColumnAttribute* node, void* data) {
  visitASTChildren(node, data);
}

void FunctionNameResolver::visitASTColumnAttributeList(
    const ASTColumnAttributeList* node, void* data) {
  visitASTChildren(node, data);
}

void FunctionNameResolver::visitASTColumnDefinition(const ASTColumnDefinition* node,
                                        void* data) {
  return;
}

void FunctionNameResolver::visitASTCreateViewStatement(
    const ASTCreateViewStatement* node, void* data) {
  node->query()->Accept(this, data);
}

void FunctionNameResolver::visitASTCreateMaterializedViewStatement(
    const ASTCreateMaterializedViewStatement* node, void* data) {
  node->query()->Accept(this, data);
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

void FunctionNameResolver::visitASTExportDataStatement(
    const ASTExportDataStatement* node, void* data) {
  node->query()->Accept(this, data);
}

void FunctionNameResolver::visitASTExportModelStatement(const ASTExportModelStatement* node,
                                            void* data) {
  return;
}

void FunctionNameResolver::visitASTWithConnectionClause(const ASTWithConnectionClause* node,
                                            void* data) {
  node->connection_clause()->Accept(this, data);
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

void FunctionNameResolver::visitASTFunctionCall(const ASTFunctionCall* node, void* data) {
  visitASTChildren(node, data);
}

void FunctionNameResolver::visitASTArrayElement(const ASTArrayElement* node, void* data) {
  visitASTChildren(node, data);
}

void FunctionNameResolver::visitASTAnalyticFunctionCall(const ASTAnalyticFunctionCall* node,
                                            void* data) {
  visitASTChildren(node, data);
}

}  // namespace function_name_resolver
}  // namespace alphasql
