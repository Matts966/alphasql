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

#include "zetasql/base/logging.h"
#include "zetasql/parser/parse_tree.h"
#include "zetasql/parser/parse_tree_visitor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace alphasql {

using namespace parser;
using namespace zetasql;

namespace function_name_resolver {

struct function_info {
  std::vector<std::vector<std::string>>> called;
  std::vector<std::vector<std::string>>> defined;
}

zetasql_base::StatusOr<function_info> GetFunctionInformation(
    const std::string& sql_file_path, const AnalyzerOptions& analyzer_options) {
  ZETASQL_RETURN_IF_ERROR(ValidateAnalyzerOptions(analyzer_options));
  std::unique_ptr<AnalyzerOptions> copy;
  const AnalyzerOptions& options = GetOptionsWithArenas(&analyzer_options, &copy);
  std::unique_ptr<ParserOutput> parser_output;
  ZETASQL_RETURN_IF_ERROR(ParseScript(sql, options.GetParserOptions(),
                              options.error_message_mode(), &parser_output));
  FunctionNameResolver resolver();
  parser_output->script()->Accept(&resolver, nullptr);
  return resolver.function_information;
}

class FunctionNameResolver : public ParseTreeVisitor {
 public:
  explicit FunctionNameResolver() {}
  FunctionNameResolver(const FunctionNameResolver&) = delete;
  FunctionNameResolver& operator=(const FunctionNameResolver&) = delete;
  ~FunctionNameResolver() override {}

  function_info function_information;

  virtual void defaultVisit(const ASTNode* node, void* data) {
    visitASTChildren(node, data);
  }

  void visitASTChildren(const ASTNode* node, void* data) {
    node->ChildrenAccept(this, data);
  }

  void visit(const ASTNode* node, void* data) override {
    visitASTChildren(node, data);
  }

  // Visitor implementation.
  void visitASTHintedStatement(const ASTHintedStatement* node,
                               void* data) override;
  void visitASTExplainStatement(const ASTExplainStatement* node,
                                void* data) override;
  void visitASTQueryStatement(const ASTQueryStatement* node,
                              void* data) override;
  void visitASTTableClause(const ASTTableClause* node, void* data) override;
  void visitASTModelClause(const ASTModelClause* node, void* data) override;
  void visitASTConnectionClause(const ASTConnectionClause* node,
                                void* data) override;
  void visitASTTVF(const ASTTVF* node, void* data) override;
  void visitASTTVFArgument(const ASTTVFArgument* node, void* data) override;
  void visitASTTVFSchema(const ASTTVFSchema* node, void* data) override;
  void visitASTTVFSchemaColumn(const ASTTVFSchemaColumn* node,
                                 void* data) override;
  void visitASTCreateConstantStatement(const ASTCreateConstantStatement* node,
                                       void* data) override;
  void visitASTCreateDatabaseStatement(const ASTCreateDatabaseStatement* node,
                                       void* data) override;
  void visitASTCreateFunctionStatement(const ASTCreateFunctionStatement* node,
                                       void* data) override;
  void visitASTCreateIndexStatement(const ASTCreateIndexStatement* node,
                                    void* data) override;
  void visitASTCreateModelStatement(const ASTCreateModelStatement* node,
                                    void* data) override;
  void visitASTCreateTableStatement(const ASTCreateTableStatement* node,
                                    void* data) override;
  void visitASTCreateEntityStatement(const ASTCreateEntityStatement* node,
                                     void* data) override;
  void visitASTAlterEntityStatement(const ASTAlterEntityStatement* node,
                                    void* data) override;
  void visitASTCreateTableFunctionStatement(
      const ASTCreateTableFunctionStatement* node, void* data) override;
  void visitASTCreateViewStatement(const ASTCreateViewStatement* node,
                                   void* data) override;
  void visitASTCreateMaterializedViewStatement(
      const ASTCreateMaterializedViewStatement* node, void* data) override;
  void visitASTWithPartitionColumnsClause(
      const ASTWithPartitionColumnsClause* node, void* data) override;
  void visitASTCreateExternalTableStatement(
      const ASTCreateExternalTableStatement* node, void* data) override;
  void visitASTCreateRowAccessPolicyStatement(
      const ASTCreateRowAccessPolicyStatement* node, void* data) override;
  void visitASTExportDataStatement(const ASTExportDataStatement* node,
                                   void* data) override;
  void visitASTExportModelStatement(const ASTExportModelStatement* node,
                                    void* data) override;
  void visitASTCallStatement(const ASTCallStatement* node,
                             void* data) override;
  void visitASTDefineTableStatement(const ASTDefineTableStatement* node,
                                    void* data) override;
  void visitASTDescribeStatement(const ASTDescribeStatement* node,
                                 void* data) override;
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
  void visitASTAlterColumnOptionsAction(const ASTAlterColumnOptionsAction* node,
                                        void* data) override;
  void visitASTAlterColumnTypeAction(const ASTAlterColumnTypeAction* node,
                                     void* data) override;
  void visitASTDropColumnAction(const ASTDropColumnAction* node,
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
  void visitASTWithClause(const ASTWithClause* node, void* data) override;
  void visitASTWithClauseEntry(const ASTWithClauseEntry* node,
                               void* data) override;
  void visitASTQuery(const ASTQuery* node, void* data) override;
  void visitASTSetOperation(const ASTSetOperation* node, void* data) override;
  void visitASTSelect(const ASTSelect* node, void* data) override;
  void visitASTSelectAs(const ASTSelectAs* node, void* data) override;
  void visitASTSelectList(const ASTSelectList* node, void* data) override;
  void visitASTSelectColumn(const ASTSelectColumn* node, void* data) override;
  void visitASTAlias(const ASTAlias* node, void* data) override;
  void visitASTIntoAlias(const ASTIntoAlias* node, void* data) override;
  void visitASTFromClause(const ASTFromClause* node, void* data) override;
  void visitASTTransformClause(const ASTTransformClause* node,
                               void* data) override;
  void visitASTWithOffset(const ASTWithOffset* node, void* data) override;
  void visitASTUnnestExpression(const ASTUnnestExpression* node,
                                void* data) override;
  void visitASTUnnestExpressionWithOptAliasAndOffset(
      const ASTUnnestExpressionWithOptAliasAndOffset* node,
      void* data) override;
  void visitASTTableElementList(const ASTTableElementList* node,
                                void* data) override;
  void visitASTTablePathExpression(const ASTTablePathExpression* node,
                                   void* data) override;
  void visitASTForSystemTime(const ASTForSystemTime* node, void* data) override;
  void visitASTTableSubquery(const ASTTableSubquery* node, void* data) override;
  void visitASTJoin(const ASTJoin* node, void* data) override;
  void visitASTParenthesizedJoin(const ASTParenthesizedJoin* node,
                                 void* data) override;
  void visitASTOnClause(const ASTOnClause* node, void* data) override;
  void visitASTOnOrUsingClauseList(const ASTOnOrUsingClauseList* node,
      void *data) override;
  void visitASTUsingClause(const ASTUsingClause* node, void* data) override;
  void visitASTWhereClause(const ASTWhereClause* node, void* data) override;
  void visitASTRollup(const ASTRollup* node, void* data) override;
  void visitASTGeneratedColumnInfo(const ASTGeneratedColumnInfo* node,
                                   void* data) override;
  void visitASTGroupingItem(const ASTGroupingItem* node, void* data) override;
  void visitASTGroupBy(const ASTGroupBy* node, void* data) override;
  void visitASTHaving(const ASTHaving* node, void* data) override;
  void visitASTCollate(const ASTCollate* node, void* data) override;
  void visitASTNullOrder(const ASTNullOrder* node, void* data) override;
  void visitASTColumnPosition(const ASTColumnPosition* node,
                              void* data) override;
  void visitASTOrderBy(const ASTOrderBy* node, void* data) override;
  void visitASTLambda(const ASTLambda* node, void* data) override;
  void visitASTLimitOffset(const ASTLimitOffset* node, void* data) override;
  void visitASTHavingModifier(const ASTHavingModifier* node,
                              void* data) override;
  void visitASTOrderingExpression(const ASTOrderingExpression* node,
                                  void* data) override;
  void visitASTIdentifier(const ASTIdentifier* node, void* data) override;
  void visitASTNewConstructorArg(const ASTNewConstructorArg* node,
                                 void* data) override;
  void visitASTNewConstructor(const ASTNewConstructor* node,
                              void* data) override;
  void visitASTInferredTypeColumnSchema(const ASTInferredTypeColumnSchema* node,
                                        void* data) override;
  void visitASTArrayConstructor(const ASTArrayConstructor* node,
                                void* data) override;
  void visitASTStructConstructorArg(const ASTStructConstructorArg* node,
                                    void* data) override;
  void visitASTStructConstructorWithParens(
      const ASTStructConstructorWithParens* node, void* data) override;
  void visitASTStructConstructorWithKeyword(
      const ASTStructConstructorWithKeyword* node, void* data) override;
  void visitASTIntLiteral(const ASTIntLiteral* node, void* data) override;
  void visitASTNumericLiteral(
      const ASTNumericLiteral* node, void* data) override;
  void visitASTBigNumericLiteral(const ASTBigNumericLiteral* node,
                                 void* data) override;
  void visitASTJSONLiteral(const ASTJSONLiteral* node,
                           void* data) override;
  void visitASTFloatLiteral(const ASTFloatLiteral* node, void* data) override;
  void visitASTStringLiteral(const ASTStringLiteral* node, void* data) override;
  void visitASTBytesLiteral(const ASTBytesLiteral* node, void* data) override;
  void visitASTBooleanLiteral(const ASTBooleanLiteral* node,
                              void* data) override;
  void visitASTNullLiteral(const ASTNullLiteral* node, void* data) override;
  void visitASTDateOrTimeLiteral(const ASTDateOrTimeLiteral* node,
                                 void* data) override;
  void visitASTStar(const ASTStar* node, void* data) override;
  void visitASTStarExceptList(const ASTStarExceptList* node,
                              void* data) override;
  void visitASTStarReplaceItem(const ASTStarReplaceItem* node,
                               void* data) override;
  void visitASTStarModifiers(const ASTStarModifiers* node, void* data) override;
  void visitASTStarWithModifiers(const ASTStarWithModifiers* node,
                                 void* data) override;
  void visitASTPathExpression(const ASTPathExpression* node,
                              void* data) override;
  void visitASTParameterExpr(const ASTParameterExpr* node, void* data) override;
  void visitASTSystemVariableExpr(const ASTSystemVariableExpr* node,
                                  void* data) override;
  void visitASTIntervalExpr(const ASTIntervalExpr* node, void* data) override;
  void visitASTDotIdentifier(const ASTDotIdentifier* node, void* data) override;
  void visitASTDotGeneralizedField(const ASTDotGeneralizedField* node,
                                   void* data) override;
  void visitASTDotStar(const ASTDotStar* node, void* data) override;
  void visitASTDotStarWithModifiers(const ASTDotStarWithModifiers* node,
                                    void* data) override;
  void visitASTOrExpr(const ASTOrExpr* node, void* data) override;
  void visitASTAndExpr(const ASTAndExpr* node, void* data) override;
  void visitASTUnaryExpression(const ASTUnaryExpression* node,
                               void* data) override;
  void visitASTCaseNoValueExpression(const ASTCaseNoValueExpression* node,
                                     void* data) override;
  void visitASTCaseValueExpression(const ASTCaseValueExpression* node,
                                   void* data) override;
  void visitASTCastExpression(const ASTCastExpression* node,
                              void* data) override;
  void visitASTExtractExpression(const ASTExtractExpression* node,
                                 void* data) override;
  void visitASTBinaryExpression(const ASTBinaryExpression* node,
                                void* data) override;
  void visitASTBitwiseShiftExpression(const ASTBitwiseShiftExpression* node,
                                      void* data) override;
  void visitASTInExpression(const ASTInExpression* node, void* data) override;
  void visitASTInList(const ASTInList* node, void* data) override;
  void visitASTIndexItemList(const ASTIndexItemList* node, void* data) override;
  void visitASTIndexStoringExpressionList(
      const ASTIndexStoringExpressionList* node, void* data) override;
  void visitASTIndexUnnestExpressionList(
      const ASTIndexUnnestExpressionList* node, void* data) override;
  void visitASTBetweenExpression(const ASTBetweenExpression* node,
                                 void* data) override;
  void visitASTFunctionCall(const ASTFunctionCall* node, void* data) override;
  void visitASTArrayElement(const ASTArrayElement* node, void* data) override;
  void visitASTExpressionSubquery(const ASTExpressionSubquery* node,
                                  void* data) override;
  void visitASTTemplatedParameterType(
      const ASTTemplatedParameterType* node, void* data) override;
  void visitASTFunctionParameter(
      const ASTFunctionParameter* node, void* data) override;
  void visitASTFunctionParameters(
      const ASTFunctionParameters* node, void* data) override;
  void visitASTFunctionDeclaration(
      const ASTFunctionDeclaration* node, void* data) override;
  void visitASTSqlFunctionBody(
      const ASTSqlFunctionBody* node, void* data) override;
  void visitASTHint(const ASTHint* node, void* data) override;
  void visitASTHintEntry(const ASTHintEntry* node, void* data) override;
  void visitASTOptionsList(const ASTOptionsList* node, void* data) override;
  void visitASTOptionsEntry(const ASTOptionsEntry* node, void* data) override;
  void visitASTSimpleType(const ASTSimpleType* node, void* data) override;
  void visitASTArrayType(const ASTArrayType* node, void* data) override;
  void visitASTStructType(const ASTStructType* node, void* data) override;
  void visitASTStructField(const ASTStructField* node, void* data) override;
  void visitASTSimpleColumnSchema(const ASTSimpleColumnSchema* node,
                                  void* data) override;
  void visitASTArrayColumnSchema(const ASTArrayColumnSchema* node,
                                 void* data) override;
  void visitASTStructColumnSchema(const ASTStructColumnSchema* node,
                                  void* data) override;
  void visitASTStructColumnField(const ASTStructColumnField* node,
                                 void* data) override;
  void visitASTAnalyticFunctionCall(const ASTAnalyticFunctionCall* node,
                                    void* data) override;
  void visitASTWindowClause(const ASTWindowClause* node, void* data) override;
  void visitASTWindowDefinition(const ASTWindowDefinition* node,
                                void* data) override;
  void visitASTWindowSpecification(const ASTWindowSpecification* node,
                                   void* data) override;
  void visitASTPartitionBy(const ASTPartitionBy* node, void* data) override;
  void visitASTClusterBy(const ASTClusterBy* node, void* data) override;
  void visitASTWindowFrame(const ASTWindowFrame* node, void* data) override;
  void visitASTWindowFrameExpr(const ASTWindowFrameExpr* node,
                               void* data) override;

  void visitASTDefaultLiteral(const ASTDefaultLiteral* node,
                              void* data) override;
  void visitASTAssertRowsModified(const ASTAssertRowsModified* node,
                                  void* data) override;
  void visitASTAssertStatement(const ASTAssertStatement* node,
                               void* data) override;
  void visitASTDeleteStatement(const ASTDeleteStatement* node,
                               void* data) override;
  void visitASTColumnAttributeList(
      const ASTColumnAttributeList* node, void* data) override;
  void visitASTNotNullColumnAttribute(
      const ASTNotNullColumnAttribute* node, void* data) override;
  void visitASTHiddenColumnAttribute(
      const ASTHiddenColumnAttribute* node, void* data) override;
  void visitASTPrimaryKeyColumnAttribute(
      const ASTPrimaryKeyColumnAttribute* node, void* data) override;
  void visitASTForeignKeyColumnAttribute(
      const ASTForeignKeyColumnAttribute* node, void* data) override;
  void visitASTColumnDefinition(const ASTColumnDefinition* node,
                                void* data) override;
  void visitASTColumnList(const ASTColumnList* node, void* data) override;
  void visitASTInsertValuesRow(const ASTInsertValuesRow* node,
                               void* data) override;
  void visitASTInsertValuesRowList(const ASTInsertValuesRowList* node,
                                   void* data) override;
  void visitASTInsertStatement(const ASTInsertStatement* node,
                               void* data) override;
  void visitASTUpdateSetValue(const ASTUpdateSetValue* node,
                              void* data) override;
  void visitASTUpdateItem(const ASTUpdateItem* node, void* data) override;
  void visitASTUpdateItemList(const ASTUpdateItemList* node,
                              void* data) override;
  void visitASTUpdateStatement(const ASTUpdateStatement* node,
                               void* data) override;
  void visitASTTruncateStatement(const ASTTruncateStatement* node,
                                 void* data) override;
  void visitASTMergeAction(const ASTMergeAction* node, void* data) override;
  void visitASTMergeWhenClause(const ASTMergeWhenClause* node,
                               void* data) override;
  void visitASTMergeWhenClauseList(const ASTMergeWhenClauseList* node,
                                   void* data) override;
  void visitASTMergeStatement(const ASTMergeStatement* node,
                              void* data) override;
  void visitASTPrimaryKey(const ASTPrimaryKey* node, void* data) override;
  void visitASTPrivilege(const ASTPrivilege* node, void* data) override;
  void visitASTPrivileges(const ASTPrivileges* node, void* data) override;
  void visitASTGranteeList(const ASTGranteeList* node, void* data) override;
  void visitASTGrantStatement(const ASTGrantStatement* node,
                              void* data) override;
  void visitASTRevokeStatement(const ASTRevokeStatement* node,
                               void* data) override;

  void visitASTRepeatableClause(const ASTRepeatableClause* node,
                                void* data) override;
  void visitASTReplaceFieldsArg(const ASTReplaceFieldsArg* node,
                                void* data) override;
  void visitASTReplaceFieldsExpression(const ASTReplaceFieldsExpression* node,
                                       void* data) override;
  void visitASTSampleSize(const ASTSampleSize* node, void* data) override;
  void visitASTSampleSuffix(const ASTSampleSuffix* node, void* data) override;
  void visitASTWithWeight(const ASTWithWeight* node, void *data) override;
  void visitASTWithConnectionClause(const ASTWithConnectionClause* node,
                                    void* data) override;
  void visitASTSampleClause(const ASTSampleClause* node, void* data) override;
  void visitASTAlterMaterializedViewStatement(
      const ASTAlterMaterializedViewStatement* node, void* data) override;
  void visitASTAlterDatabaseStatement(const ASTAlterDatabaseStatement* node,
                                      void* data) override;
  void visitASTAlterTableStatement(const ASTAlterTableStatement* node,
                                   void* data) override;
  void visitASTAlterViewStatement(const ASTAlterViewStatement* node,
                                  void* data) override;
  void visitASTSetOptionsAction(const ASTSetOptionsAction* node,
                                   void* data) override;
  void visitASTSetAsAction(const ASTSetAsAction* node, void* data) override;
  void visitASTAddConstraintAction(const ASTAddConstraintAction* node,
                                   void* data) override;
  void visitASTDropConstraintAction(const ASTDropConstraintAction* node,
                                    void* data) override;
  void visitASTAlterConstraintEnforcementAction(
      const ASTAlterConstraintEnforcementAction* node, void* data) override;
  void visitASTAlterConstraintSetOptionsAction(
      const ASTAlterConstraintSetOptionsAction* node, void* data) override;
  void visitASTAddColumnAction(const ASTAddColumnAction* node,
                               void* data) override;
  void visitASTGrantToClause(const ASTGrantToClause* node, void* data) override;
  void visitASTFilterUsingClause(const ASTFilterUsingClause* node,
                                 void* data) override;
  void visitASTRevokeFromClause(const ASTRevokeFromClause* node,
                                void* data) override;
  void visitASTRenameToClause(const ASTRenameToClause* node,
                              void* data) override;
  void visitASTAlterActionList(const ASTAlterActionList* node,
                               void* data) override;
  void visitASTDescriptorColumn(const ASTDescriptorColumn* node,
                                void* data) override;
  void visitASTDescriptorColumnList(const ASTDescriptorColumnList* node,
                                    void* data) override;
  void visitASTDescriptor(const ASTDescriptor* node, void* data) override;
  void visitASTAlterRowAccessPolicyStatement(
      const ASTAlterRowAccessPolicyStatement* node, void* data) override;

  void visitASTAlterAllRowAccessPoliciesStatement(
    const ASTAlterAllRowAccessPoliciesStatement* node, void* data) override;

  void visitASTForeignKey(const ASTForeignKey* node, void* data) override;
  void visitASTForeignKeyReference(
      const ASTForeignKeyReference* node, void* data) override;
  void visitASTForeignKeyActions(
      const ASTForeignKeyActions* node, void* data) override;

  void visitASTExceptionHandler(const ASTExceptionHandler* node,
                                void* data) override;
  void visitASTExceptionHandlerList(const ASTExceptionHandlerList* node,
                                    void* data) override;
  void visitASTStatementList(const ASTStatementList* node, void* data) override;
  void visitASTIfStatement(const ASTIfStatement* node, void* data) override;
  void visitASTElseifClause(const ASTElseifClause* node, void* data) override;
  void visitASTElseifClauseList(const ASTElseifClauseList* node,
                                void* data) override;
  void visitASTBeginEndBlock(const ASTBeginEndBlock* node, void* data) override;
  void visitASTIdentifierList(const ASTIdentifierList* node,
                              void* data) override;
  void visitASTVariableDeclaration(const ASTVariableDeclaration* node,
                                   void* data) override;
  void visitASTParameterAssignment(const ASTParameterAssignment* node,
                                   void* data) override;
  void visitASTSystemVariableAssignment(const ASTSystemVariableAssignment* node,
                                        void* data) override;
  void visitASTSingleAssignment(const ASTSingleAssignment* node,
                                void* data) override;

  void visitASTCheckConstraint(const ASTCheckConstraint* node,
                               void* data) override;
  void visitASTScript(const ASTScript* node, void* data) override;
  void visitASTWhileStatement(const ASTWhileStatement* node,
                              void* data) override;
  void visitASTBreakStatement(const ASTBreakStatement* node,
                              void* data) override;
  void visitASTContinueStatement(const ASTContinueStatement* node,
                                 void* data) override;
  void visitASTReturnStatement(const ASTReturnStatement* node,
                               void* data) override;
  void visitASTAssignmentFromStruct(const ASTAssignmentFromStruct* node,
                                    void* data) override;
  void visitASTCreateProcedureStatement(const ASTCreateProcedureStatement* node,
                                        void* data) override;
  void visitASTNamedArgument(const ASTNamedArgument* node, void* data) override;
  void visitASTExecuteIntoClause(const ASTExecuteIntoClause* node,
                                 void* data) override;
  void visitASTExecuteUsingArgument(const ASTExecuteUsingArgument* node,
                                    void* data) override;
  void visitASTExecuteUsingClause(const ASTExecuteUsingClause* node,
                                  void* data) override;
  void visitASTExecuteImmediateStatement(
      const ASTExecuteImmediateStatement* node, void* data) override;
  void visitASTRaiseStatement(const ASTRaiseStatement* node,
                              void* data) override;
};

}  // namespace alphasql
}  // namespace function_name_resolver

#endif  // ALPHASQL_FUNCTION_NAME_RESOLVER_H_
