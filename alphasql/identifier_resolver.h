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

// TODO This implementation probably doesn't cover all edge cases for
// table name extraction.  It should be tested more and tuned for the final
// name scoping rules once those are implemented in the full resolver.

namespace zetasql {
namespace identifier_resolver {
namespace {

// Each instance should be used only once.
class IdentifierResolver {
 public:
  // <*analyzer_options> must outlive the created IdentifierResolver. It must
  // have all arenas initialized.
  // If 'type_factory' and 'catalog' are not null, their contents must
  // outlive the created IdentifierResolver as well.
  //
  IdentifierResolver(
      absl::string_view sql, const AnalyzerOptions* analyzer_options,
      TypeFactory* type_factory, Catalog* catalog, TableNamesSet* table_names,
      TableResolutionTimeInfoMap* table_resolution_time_info_map)
    : sql_(sql), analyzer_options_(analyzer_options),
      for_system_time_as_of_feature_enabled_(
          analyzer_options->language().LanguageFeatureEnabled(
              FEATURE_V_1_1_FOR_SYSTEM_TIME_AS_OF)),
      type_factory_(type_factory), catalog_(catalog),
      table_names_(table_names),
      table_resolution_time_info_map_(table_resolution_time_info_map) {
    DCHECK(analyzer_options_->AllArenasAreInitialized());
  }

  IdentifierResolver(const IdentifierResolver&) = delete;
  IdentifierResolver& operator=(const IdentifierResolver&) = delete;

  absl::Status FindTableNamesAndTemporalReferences(
      const ASTStatement& statement);

  absl::Status FindTableNames(const ASTScript& script);

  std::map<ResolvedNodeKind, TableNamesSet> node_kind_to_table_names() {
    return _node_kind_to_table_names;
  }

  absl::Status FindInStatement(const ASTStatement* statement);

 private:
  typedef std::set<std::string> AliasSet;  // Always lowercase.

  std::map<ResolvedNodeKind, TableNamesSet> _node_kind_to_table_names;

  // Consumes either an ASTScript, ASTStatementList, or ASTScriptStatement.
  absl::Status FindInScriptNode(const ASTNode* node);

  absl::Status FindInQueryStatement(const ASTQueryStatement* statement);

  absl::Status FindInCreateViewStatement(
      const ASTCreateViewStatement* statement);

  absl::Status FindInCreateMaterializedViewStatement(
      const ASTCreateMaterializedViewStatement* statement);

  absl::Status FindInCreateTableFunctionStatement(
      const ASTCreateTableFunctionStatement* statement);

  absl::Status FindInExportDataStatement(
      const ASTExportDataStatement* statement);

  absl::Status FindInDeleteStatement(const ASTDeleteStatement* statement);

  absl::Status FindInTruncateStatement(const ASTTruncateStatement* statement);

  absl::Status FindInInsertStatement(const ASTInsertStatement* statement);

  absl::Status FindInUpdateStatement(const ASTUpdateStatement* statement);

  absl::Status FindInMergeStatement(const ASTMergeStatement* statement);

  // 'visible_aliases' includes things like the table name we are inserting
  // into or deleting from.  It does *not* include WITH table aliases or TVF
  // table-valued argument names (which are both tracked separately in
  // 'local_table_aliases_').
  absl::Status FindInQuery(const ASTQuery* query,
                           const AliasSet& visible_aliases);

  absl::Status FindInQueryExpression(const ASTQueryExpression* query_expr,
                                     const ASTOrderBy* order_by,
                                     const AliasSet& visible_aliases);

  absl::Status FindInSelect(const ASTSelect* select, const ASTOrderBy* order_by,
                            const AliasSet& orig_visible_aliases);

  absl::Status FindInSetOperation(const ASTSetOperation* set_operation,
                                  const AliasSet& visible_aliases);

  // When resolving the FROM clause, <external_visible_aliases> is the set
  // of names visible in the query without any names from the FROM clause
  // (excluding WITH names and TVF table-valued argument names).
  // <local_visible_aliases> includes all names visible in
  // <external_visible_alaises> plus names earlier in the same FROM clause
  // that are visible.  See corresponding methods in resolver.cc.
  absl::Status FindInTableExpression(const ASTTableExpression* table_expr,
                                     const AliasSet& external_visible_aliases,
                                     AliasSet* local_visible_aliases);

  absl::Status FindInJoin(const ASTJoin* join,
                          const AliasSet& external_visible_aliases,
                          AliasSet* local_visible_aliases);

  absl::Status FindInParenthesizedJoin(
      const ASTParenthesizedJoin* parenthesized_join,
      const AliasSet& external_visible_aliases,
      AliasSet* local_visible_aliases);

  absl::Status FindInTVF(
      const ASTTVF* tvf,
      const AliasSet& external_visible_aliases,
      AliasSet* local_visible_aliases);

  absl::Status FindInTableSubquery(
      const ASTTableSubquery* table_subquery,
      const AliasSet& external_visible_aliases,
      AliasSet* local_visible_aliases);

  absl::Status FindInTablePathExpression(
      const ASTTablePathExpression* table_ref,
      AliasSet* visible_aliases);

  // Traverse all expressions attached as descendants of <root>.
  // Unlike other methods above, may be called with NULL.
  absl::Status FindInExpressionsUnder(const ASTNode* root,
                                      const AliasSet& visible_aliases);

  // Traverse all options_list node as descendants of <root>.
  // May be called with NULL.
  absl::Status FindInOptionsListUnder(const ASTNode* root,
                                      const AliasSet& visible_aliases);

  // Root level SQL statement we are extracting table names or temporal
  // references from.
  const absl::string_view sql_;

  const AnalyzerOptions* analyzer_options_;  // Not owned.

  const bool for_system_time_as_of_feature_enabled_;

  TypeFactory* type_factory_;

  Catalog* catalog_;

  // The set of table names we are building up in this call to FindTables.
  // NOTE: The raw pointer is not owned.  We just cache the output parameter
  // to FindTables/FindTableNamesAndTemporalReferences to simplify sharing
  // across recursive calls.
  TableNamesSet* table_names_ = nullptr;

  // The set of temporal table references we are building up
  // in this call to FindTemporalTableReferencess.
  // NOTE: The raw pointer is not owned.  We just cache the output parameter
  // to FindTableNamesAndTemporalReferences to simplify sharing
  // across recursive calls.
  TableResolutionTimeInfoMap* table_resolution_time_info_map_ = nullptr;

  // The set of local table aliases, including TVF table-valued argument
  // aliases and in-scope WITH aliases.
  AliasSet local_table_aliases_;

  // When inside a CREATE RECURSIVE VIEW statement, the name of the view; such
  // names should be treated similar to a WITH alias and not be considered an
  // external reference. In all other cases, this field is an empty vector.
  std::vector<std::string> recursive_view_name_;
};

absl::Status IdentifierResolver::FindTableNames(const ASTScript& script) {
  table_names_->clear();
  ZETASQL_RETURN_IF_ERROR(FindInScriptNode(&script));
  // Sanity check - these should get popped.
  ZETASQL_RET_CHECK(local_table_aliases_.empty());
  return absl::OkStatus();
}

absl::Status IdentifierResolver::FindInScriptNode(const ASTNode* node) {
  for (int i = 0; i < node->num_children(); ++i) {
    const ASTNode* child = node->child(i);
    if (child->IsExpression()) {
      ZETASQL_RETURN_IF_ERROR(FindInExpressionsUnder(child, /*visible_aliases=*/{}));
    } else if (child->IsSqlStatement()) {
      ZETASQL_RETURN_IF_ERROR(FindInStatement(child->GetAs<ASTStatement>()));
    }
    ZETASQL_RETURN_IF_ERROR(FindInScriptNode(child));
  }
  return absl::OkStatus();
}

absl::Status IdentifierResolver::FindInStatement(const ASTStatement* statement) {
  // Find table name under OPTIONS (...) clause for any type of statement.
  ZETASQL_RETURN_IF_ERROR(FindInOptionsListUnder(statement, /*visible_aliases=*/{}));
  switch (statement->node_kind()) {
    case AST_QUERY_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_QUERY_STMT)) {
        return FindInQueryStatement(
            static_cast<const ASTQueryStatement*>(statement));
      }
      break;

    case AST_EXPLAIN_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_EXPLAIN_STMT)) {
        const ASTExplainStatement* explain =
            static_cast<const ASTExplainStatement*>(statement);
        return FindInStatement(explain->statement());
      }
      break;

    case AST_CREATE_DATABASE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_DATABASE_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_CREATE_INDEX_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_INDEX_STMT)) {
        const ASTCreateIndexStatement* create_index =
            static_cast<const ASTCreateIndexStatement*>(statement);
        zetasql_base::InsertIfNotPresent(
            table_names_, create_index->table_name()->ToIdentifierVector());
        return absl::OkStatus();
      }
      break;

    case AST_CREATE_TABLE_STATEMENT: {
      const ASTCreateTableStatement* create_statement = statement->GetAs<ASTCreateTableStatement>();
      const ASTQuery* query = create_statement->query();
      if (query == nullptr) {
        if (analyzer_options_->language().SupportsStatementKind(
                RESOLVED_CREATE_TABLE_STMT)) {
          _node_kind_to_table_names[RESOLVED_CREATE_TABLE_STMT].insert(
            create_statement->name()->ToIdentifierVector()
          );
          return absl::OkStatus();
        }
      } else {
        if (analyzer_options_->language().SupportsStatementKind(
                RESOLVED_CREATE_TABLE_AS_SELECT_STMT)) {
          _node_kind_to_table_names[RESOLVED_CREATE_TABLE_AS_SELECT_STMT].insert(
            create_statement->name()->ToIdentifierVector()
          );
          return FindInQuery(query, /*visible_aliases=*/{});
        }
      }
      break;
    }
    case AST_CREATE_MODEL_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_MODEL_STMT)) {
        const ASTQuery* query =
            statement->GetAs<ASTCreateModelStatement>()->query();
        if (query == nullptr) {
          return absl::OkStatus();
        }
        return FindInQuery(query, /*visible_aliases=*/{});
      }
      break;
    case AST_CREATE_VIEW_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_VIEW_STMT)) {
        return FindInCreateViewStatement(
            statement->GetAs<ASTCreateViewStatement>());
      }
      break;
    case AST_CREATE_MATERIALIZED_VIEW_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_MATERIALIZED_VIEW_STMT)) {
        return FindInCreateMaterializedViewStatement(
            statement->GetAs<ASTCreateMaterializedViewStatement>());
      }
      break;

    case AST_CREATE_EXTERNAL_TABLE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_EXTERNAL_TABLE_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_CREATE_ROW_ACCESS_POLICY_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_ROW_ACCESS_POLICY_STMT)) {
        const ASTCreateRowAccessPolicyStatement* stmt =
            statement->GetAsOrDie<ASTCreateRowAccessPolicyStatement>();
        zetasql_base::InsertIfNotPresent(table_names_,
                                stmt->target_path()->ToIdentifierVector());
        return FindInExpressionsUnder(stmt->filter_using()->predicate(),
                                      /*visible_aliases=*/{});
      }
      break;

    case AST_CREATE_CONSTANT_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_CONSTANT_STMT)) {
        ZETASQL_RETURN_IF_ERROR(FindInExpressionsUnder(
            static_cast<const ASTCreateConstantStatement*>(statement)->expr(),
            /*visible_aliases=*/{}));
        return absl::OkStatus();
      }
      break;

    case AST_CREATE_FUNCTION_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_FUNCTION_STMT)) {
        ZETASQL_RETURN_IF_ERROR(FindInExpressionsUnder(
            static_cast<const ASTCreateFunctionStatement*>(statement)
                ->sql_function_body(),
            /*visible_aliases=*/{}));
        return absl::OkStatus();
      }
      break;

    case AST_CREATE_TABLE_FUNCTION_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_TABLE_FUNCTION_STMT)) {
        return FindInCreateTableFunctionStatement(
            statement->GetAs<ASTCreateTableFunctionStatement>());
      }
      break;

    case AST_CREATE_PROCEDURE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_PROCEDURE_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_EXPORT_DATA_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_EXPORT_DATA_STMT)) {
        return FindInExportDataStatement(
            statement->GetAs<ASTExportDataStatement>());
      }
      break;

    case AST_CALL_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CALL_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_DEFINE_TABLE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_DEFINE_TABLE_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_DESCRIBE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_DESCRIBE_STMT)) {
        // Note that for a DESCRIBE TABLE statement, the table name is not
        // inserted into table_names_. Engines that need to know about a table
        // referenced by DESCRIBE TABLE should handle that themselves.
        return absl::OkStatus();
      }
      break;

    case AST_SHOW_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_SHOW_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_BEGIN_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_BEGIN_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_SET_TRANSACTION_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_SET_TRANSACTION_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_COMMIT_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_COMMIT_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_ROLLBACK_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ROLLBACK_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_START_BATCH_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_START_BATCH_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_RUN_BATCH_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_RUN_BATCH_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_ABORT_BATCH_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ABORT_BATCH_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_DELETE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_DELETE_STMT)) {
        return FindInDeleteStatement(statement->GetAs<ASTDeleteStatement>());
      }
      break;

    case AST_DROP_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_DROP_STMT)) {
        zetasql_base::InsertIfNotPresent(table_names_,
                                static_cast<const ASTDropStatement*>(statement)->name()->ToIdentifierVector());
        return absl::OkStatus();
      }
      break;

    case AST_TRUNCATE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_TRUNCATE_STMT)) {
        return FindInTruncateStatement(
            statement->GetAsOrDie<ASTTruncateStatement>());
      }
      break;

    case AST_DROP_MATERIALIZED_VIEW_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
          RESOLVED_DROP_MATERIALIZED_VIEW_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_DROP_FUNCTION_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_DROP_FUNCTION_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_DROP_ROW_ACCESS_POLICY_STATEMENT:
    case AST_DROP_ALL_ROW_ACCESS_POLICIES_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_DROP_ROW_ACCESS_POLICY_STMT)) {
        // Note that for a DROP [ALL] ROW (ACCESS POLICY|[ACCESS] POLICIES)
        // statement, the table name is not inserted into table_names_. Engines
        // that need to know about the target table should handle that
        // themselves.
        return absl::OkStatus();
      }
      break;

    case AST_RENAME_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_RENAME_STMT)) {
        // Note that for a RENAME TABLE statement, the table names are not
        // inserted into table_names_. Engines that need to know about a table
        // referenced by RENAME TABLE should handle that themselves.
        return absl::OkStatus();
      }
      break;

    case AST_INSERT_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_INSERT_STMT)) {
        return FindInInsertStatement(statement->GetAs<ASTInsertStatement>());
      }
      break;

    case AST_UPDATE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_UPDATE_STMT)) {
        return FindInUpdateStatement(statement->GetAs<ASTUpdateStatement>());
      }
      break;

    case AST_MERGE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_MERGE_STMT)) {
        return FindInMergeStatement(statement->GetAs<ASTMergeStatement>());
      }
      break;

    case AST_GRANT_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_GRANT_STMT)) {
        // Note that for a GRANT statement, the table name is not inserted
        // into table_names_. Engines that need to know about a table
        // referenced by GRANT statement should handle that themselves.
        return absl::OkStatus();
      }
      break;

    case AST_REVOKE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_REVOKE_STMT)) {
        // Note that for a REVOKE statement, the table name is not inserted
        // into table_names_. Engines that need to know about a table
        // referenced by REVOKE statement should handle that themselves.
        return absl::OkStatus();
      }
      break;
    case AST_ALTER_ROW_ACCESS_POLICY_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ALTER_ROW_ACCESS_POLICY_STMT)) {
        const ASTAlterRowAccessPolicyStatement* stmt =
            statement->GetAs<ASTAlterRowAccessPolicyStatement>();
        zetasql_base::InsertIfNotPresent(table_names_,
                                stmt->path()->ToIdentifierVector());
        return absl::OkStatus();
      }
      break;
    case AST_ALTER_ALL_ROW_ACCESS_POLICIES_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ALTER_ALL_ROW_ACCESS_POLICIES_STMT)) {
        const ASTAlterAllRowAccessPoliciesStatement* stmt =
            statement->GetAs<ASTAlterAllRowAccessPoliciesStatement>();
        zetasql_base::InsertIfNotPresent(table_names_,
                                stmt->table_name_path()->ToIdentifierVector());
        return absl::OkStatus();
      }
      break;
    case AST_ALTER_DATABASE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ALTER_DATABASE_STMT)) {
        return absl::OkStatus();
      }
      break;
    case AST_ALTER_TABLE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ALTER_TABLE_SET_OPTIONS_STMT) ||
          analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ALTER_TABLE_STMT)) {
        // Note that for a ALTER TABLE statement, the table name is not
        // inserted into table_names_. Engines that need to know about a table
        // referenced by ALTER TABLE should handle that themselves.
        return absl::OkStatus();
      }
      break;
    case AST_ALTER_VIEW_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ALTER_VIEW_STMT)) {
        // Note that for a ALTER VIEW statement, the table name is not
        // inserted into table_names_. Engines that need to know about a table
        // referenced by ALTER VIEW should handle that themselves.
        return absl::OkStatus();
      }
      break;
    case AST_ALTER_MATERIALIZED_VIEW_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ALTER_MATERIALIZED_VIEW_STMT)) {
        // Note that for a ALTER MATERIALIZED VIEW statement, the table name is
        // not inserted into table_names_. Engines that need to know about a
        // table referenced by ALTER MATERIALIZED VIEW should handle that
        // themselves.
        return absl::OkStatus();
      }
      break;
    case AST_HINTED_STATEMENT:
      return FindInStatement(
          statement->GetAs<ASTHintedStatement>()->statement());
    case AST_IMPORT_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_IMPORT_STMT)) {
        // There are no table names in an IMPORT statement.
        return absl::OkStatus();
      }
      break;
    case AST_MODULE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_MODULE_STMT)) {
        // There are no table names in a MODULE statement.
        return absl::OkStatus();
      }
      break;
    case AST_ASSERT_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ASSERT_STMT)) {
        return FindInExpressionsUnder(
            statement->GetAs<ASTAssertStatement>()->expr(),
            /*visible_aliases=*/{});
      }
      break;
    case AST_SYSTEM_VARIABLE_ASSIGNMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ASSIGNMENT_STMT)) {
        // The LHS, a system variable, cannot reference any tables.  But, the
        // RHS expression can.
        return FindInExpressionsUnder(
            statement->GetAs<ASTSystemVariableAssignment>()->expression(),
            /*visible_aliases=*/{});
      }
      break;
    case AST_EXECUTE_IMMEDIATE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_EXECUTE_IMMEDIATE_STMT)) {
        const ASTExecuteImmediateStatement* stmt =
            statement->GetAs<ASTExecuteImmediateStatement>();
        ZETASQL_RETURN_IF_ERROR(
            FindInExpressionsUnder(stmt->using_clause(),
                                   /*visible_aliases=*/{}));
        return FindInExpressionsUnder(stmt->sql(), /*visible_aliases=*/{});
      }
      break;
    default:
      break;
  }

  // This statement is not currently supported so we return an error here.
  return MakeSqlErrorAt(statement)
         << "Statement not supported: " << statement->GetNodeKindString();
}

absl::Status IdentifierResolver::FindInQueryStatement(
    const ASTQueryStatement* statement) {
  return FindInQuery(statement->query(), /*visible_aliases=*/{});
}

absl::Status IdentifierResolver::FindInCreateViewStatement(
    const ASTCreateViewStatement* statement) {
  if (statement->recursive()) {
    recursive_view_name_ = statement->name()->ToIdentifierVector();
  }
  ZETASQL_RETURN_IF_ERROR(FindInQuery(statement->query(), /*visible_aliases=*/{}));
  recursive_view_name_.clear();
  return absl::OkStatus();
}

absl::Status IdentifierResolver::FindInCreateMaterializedViewStatement(
    const ASTCreateMaterializedViewStatement* statement) {
  if (statement->recursive()) {
    recursive_view_name_ = statement->name()->ToIdentifierVector();
  }
  ZETASQL_RETURN_IF_ERROR(FindInQuery(statement->query(), /*visible_aliases=*/{}));
  recursive_view_name_.clear();
  return absl::OkStatus();
}

absl::Status IdentifierResolver::FindInCreateTableFunctionStatement(
    const ASTCreateTableFunctionStatement* statement) {
  if (statement->query() == nullptr) {
    return absl::OkStatus();
  }
  ZETASQL_RET_CHECK(local_table_aliases_.empty());
  for (const ASTFunctionParameter* const parameter
           : statement->function_declaration()->parameters()->
               parameter_entries()) {
    if (parameter->name() == nullptr) {
      continue;
    }
    // If it's a table parameter or is ANY TABLE or ANY TYPE then it is
    // a name that we should ignore.
    if (parameter->IsTableParameter() ||
        (parameter->IsTemplated() &&
         parameter->templated_parameter_type()->kind()
           == ASTTemplatedParameterType::ANY_TABLE)) {
      zetasql_base::InsertIfNotPresent(&local_table_aliases_,
                              parameter->name()->GetAsString());
    }
  }
  ZETASQL_RETURN_IF_ERROR(FindInQuery(statement->query(), /*visible_aliases=*/{}));
  local_table_aliases_.clear();
  return absl::OkStatus();
}

absl::Status IdentifierResolver::FindInExportDataStatement(
    const ASTExportDataStatement* statement) {
  return FindInQuery(statement->query(), /*visible_aliases=*/{});
}

absl::Status IdentifierResolver::FindInDeleteStatement(
    const ASTDeleteStatement* statement) {
  ZETASQL_ASSIGN_OR_RETURN(const ASTPathExpression* path_expr,
                   statement->GetTargetPathForNonNested());
  std::vector<std::string> path = path_expr->ToIdentifierVector();
  zetasql_base::InsertIfNotPresent(table_names_, path);

  AliasSet visible_aliases;
  zetasql_base::InsertIfNotPresent(table_names_, path);
  const std::string alias = statement->alias() == nullptr
                                ? path.back()
                                : statement->alias()->GetAsString();
  zetasql_base::InsertIfNotPresent(&visible_aliases, absl::AsciiStrToLower(alias));

  ZETASQL_RETURN_IF_ERROR(FindInExpressionsUnder(statement->where(), visible_aliases));
  return absl::OkStatus();
}

absl::Status IdentifierResolver::FindInTruncateStatement(
    const ASTTruncateStatement* statement) {
  AliasSet visible_aliases;

  ZETASQL_ASSIGN_OR_RETURN(const ASTPathExpression* path_expr,
                   statement->GetTargetPathForNonNested());
  std::vector<std::string> path = path_expr->ToIdentifierVector();
  zetasql_base::InsertIfNotPresent(table_names_, path);
  zetasql_base::InsertIfNotPresent(&visible_aliases,
                          absl::AsciiStrToLower(path.back()));

  return FindInExpressionsUnder(statement->where(), visible_aliases);
}

absl::Status IdentifierResolver::FindInInsertStatement(
    const ASTInsertStatement* statement) {
  AliasSet visible_aliases;

  ZETASQL_ASSIGN_OR_RETURN(const ASTPathExpression* path_expr,
                   statement->GetTargetPathForNonNested());
  std::vector<std::string> path = path_expr->ToIdentifierVector();
  zetasql_base::InsertIfNotPresent(table_names_, path);
  _node_kind_to_table_names[RESOLVED_INSERT_STMT].insert(path);
  zetasql_base::InsertIfNotPresent(&visible_aliases,
                          absl::AsciiStrToLower(path.back()));

  if (statement->rows() != nullptr) {
    ZETASQL_RETURN_IF_ERROR(FindInExpressionsUnder(statement->rows(), visible_aliases));
  }

  if (statement->query() != nullptr) {
    ZETASQL_RETURN_IF_ERROR(FindInQuery(statement->query(), visible_aliases));
  }
  return absl::OkStatus();
}

absl::Status IdentifierResolver::FindInUpdateStatement(
    const ASTUpdateStatement* statement) {
  AliasSet visible_aliases;

  ZETASQL_ASSIGN_OR_RETURN(const ASTPathExpression* path_expr,
                   statement->GetTargetPathForNonNested());
  const std::vector<std::string> path = path_expr->ToIdentifierVector();

  zetasql_base::InsertIfNotPresent(table_names_, path);
  _node_kind_to_table_names[RESOLVED_UPDATE_STMT].insert(path);
  const std::string alias = statement->alias() == nullptr
                                ? path.back()
                                : statement->alias()->GetAsString();
  zetasql_base::InsertIfNotPresent(&visible_aliases, absl::AsciiStrToLower(alias));

  if (statement->from_clause() != nullptr) {
    ZETASQL_RET_CHECK(statement->from_clause()->table_expression() != nullptr);
    ZETASQL_RETURN_IF_ERROR(FindInTableExpression(
        statement->from_clause()->table_expression(),
        /*external_visible_aliases=*/{},
        &visible_aliases));
  }

  ZETASQL_RETURN_IF_ERROR(FindInExpressionsUnder(statement->where(), visible_aliases));
  ZETASQL_RETURN_IF_ERROR(
      FindInExpressionsUnder(statement->update_item_list(), visible_aliases));
  return absl::OkStatus();
}

absl::Status IdentifierResolver::FindInMergeStatement(
    const ASTMergeStatement* statement) {
  AliasSet visible_aliases;

  const ASTPathExpression* path_expr = statement->target_path();
  std::vector<std::string> path = path_expr->ToIdentifierVector();
  zetasql_base::InsertIfNotPresent(table_names_, path);
  zetasql_base::InsertIfNotPresent(&visible_aliases,
                          absl::AsciiStrToLower(path.back()));

  ZETASQL_RETURN_IF_ERROR(FindInTableExpression(statement->table_expression(),
                                        /*external_visible_aliases=*/{},
                                        &visible_aliases));
  ZETASQL_RETURN_IF_ERROR(FindInExpressionsUnder(statement->merge_condition(),
                                         visible_aliases));
  ZETASQL_RETURN_IF_ERROR(
      FindInExpressionsUnder(statement->when_clauses(), visible_aliases));

  return absl::OkStatus();
}

absl::Status IdentifierResolver::FindInQuery(
    const ASTQuery* query,
    const AliasSet& visible_aliases) {
  AliasSet old_local_table_aliases;
  if (query->with_clause() != nullptr) {
    // Record the set of local table aliases visible in the outer scope so we
    // can restore that after processing the local query.
    old_local_table_aliases = local_table_aliases_;

    if (query->with_clause()->recursive()) {
      // In WITH RECURSIVE, any entry can access an alias defined in any other
      // entry, regardless of declaration order.
      for (const ASTWithClauseEntry* with_entry :
           query->with_clause()->with()) {
        const std::string with_alias =
            absl::AsciiStrToLower(with_entry->alias()->GetAsString());
        zetasql_base::InsertIfNotPresent(&local_table_aliases_, with_alias);
      }
      for (const ASTWithClauseEntry* with_entry :
           query->with_clause()->with()) {
        ZETASQL_RETURN_IF_ERROR(FindInQuery(with_entry->query(), visible_aliases));
        const std::string with_alias =
            absl::AsciiStrToLower(with_entry->alias()->GetAsString());
      }
    } else {
      // In WITH without RECURSIVE, entries can only access with aliases defined
      // in prior entries.
      for (const ASTWithClauseEntry* with_entry :
           query->with_clause()->with()) {
        ZETASQL_RETURN_IF_ERROR(FindInQuery(with_entry->query(), visible_aliases));
        const std::string with_alias =
            absl::AsciiStrToLower(with_entry->alias()->GetAsString());
        zetasql_base::InsertIfNotPresent(&local_table_aliases_, with_alias);
      }
    }
  }

  ZETASQL_RETURN_IF_ERROR(FindInQueryExpression(query->query_expr(),
                                        query->order_by(),
                                        visible_aliases));

  // Restore local table alias set if we modified it.
  if (query->with_clause() != nullptr) {
    local_table_aliases_ = old_local_table_aliases;
  }
  return absl::OkStatus();
}

absl::Status IdentifierResolver::FindInQueryExpression(
    const ASTQueryExpression* query_expr,
    const ASTOrderBy* order_by,
    const AliasSet& visible_aliases) {
  switch (query_expr->node_kind()) {
    case AST_SELECT:
      ZETASQL_RETURN_IF_ERROR(
          FindInSelect(query_expr->GetAs<ASTSelect>(),
                       order_by,
                       visible_aliases));
      break;
    case AST_SET_OPERATION:
      ZETASQL_RETURN_IF_ERROR(
          FindInSetOperation(query_expr->GetAs<ASTSetOperation>(),
                             visible_aliases));
      break;
    case AST_QUERY:
      ZETASQL_RETURN_IF_ERROR(
          FindInQuery(query_expr->GetAs<ASTQuery>(), visible_aliases));
      break;
    default:
      return MakeSqlErrorAt(query_expr)
             << "Unhandled query_expr:\n" << query_expr->DebugString();
  }

  if (query_expr->node_kind() != AST_SELECT) {
    ZETASQL_RETURN_IF_ERROR(FindInExpressionsUnder(order_by, visible_aliases));
  }
  return absl::OkStatus();
}

absl::Status IdentifierResolver::FindInSelect(
    const ASTSelect* select,
    const ASTOrderBy* order_by,
    const AliasSet& orig_visible_aliases) {
  AliasSet visible_aliases = orig_visible_aliases;
  if (select->from_clause() != nullptr) {
    ZETASQL_RET_CHECK(select->from_clause()->table_expression() != nullptr);
    ZETASQL_RETURN_IF_ERROR(FindInTableExpression(
        select->from_clause()->table_expression(),
        orig_visible_aliases,
        &visible_aliases));
  }
  ZETASQL_RETURN_IF_ERROR(FindInExpressionsUnder(select->select_list(),
                                         visible_aliases));
  ZETASQL_RETURN_IF_ERROR(FindInExpressionsUnder(select->where_clause(),
                                         visible_aliases));
  ZETASQL_RETURN_IF_ERROR(FindInExpressionsUnder(select->group_by(), visible_aliases));
  ZETASQL_RETURN_IF_ERROR(FindInExpressionsUnder(select->having(), visible_aliases));
  ZETASQL_RETURN_IF_ERROR(FindInExpressionsUnder(order_by, visible_aliases));
  return absl::OkStatus();
}

absl::Status IdentifierResolver::FindInSetOperation(
    const ASTSetOperation* set_operation,
    const AliasSet& visible_aliases) {
  for (const ASTQueryExpression* input : set_operation->inputs()) {
    ZETASQL_RETURN_IF_ERROR(FindInQueryExpression(input, nullptr /* order_by */,
                                          visible_aliases));
  }
  return absl::OkStatus();
}

absl::Status IdentifierResolver::FindInTableExpression(
    const ASTTableExpression* table_expr,
    const AliasSet& external_visible_aliases,
    AliasSet* local_visible_aliases) {
  switch (table_expr->node_kind()) {
    case AST_TABLE_PATH_EXPRESSION:
      return FindInTablePathExpression(
          table_expr->GetAs<ASTTablePathExpression>(), local_visible_aliases);

    case AST_TABLE_SUBQUERY:
      return FindInTableSubquery(
          table_expr->GetAs<ASTTableSubquery>(),
          external_visible_aliases, local_visible_aliases);

    case AST_JOIN:
      return FindInJoin(table_expr->GetAs<ASTJoin>(),
                        external_visible_aliases, local_visible_aliases);

    case AST_PARENTHESIZED_JOIN:
      return FindInParenthesizedJoin(table_expr->GetAs<ASTParenthesizedJoin>(),
                                     external_visible_aliases,
                                     local_visible_aliases);

    case AST_TVF:
      return FindInTVF(table_expr->GetAs<ASTTVF>(), external_visible_aliases,
                       local_visible_aliases);
    default:
      return MakeSqlErrorAt(table_expr)
             << "Unhandled node type in from clause: "
             << table_expr->GetNodeKindString();
  }
}

absl::Status IdentifierResolver::FindInJoin(
    const ASTJoin* join,
    const AliasSet& external_visible_aliases,
    AliasSet* local_visible_aliases) {
  ZETASQL_RETURN_IF_ERROR(FindInTableExpression(join->lhs(), external_visible_aliases,
                                        local_visible_aliases));
  ZETASQL_RETURN_IF_ERROR(FindInTableExpression(join->rhs(), external_visible_aliases,
                                        local_visible_aliases));
  ZETASQL_RETURN_IF_ERROR(FindInExpressionsUnder(join->on_clause(),
                                         *local_visible_aliases));
  return absl::OkStatus();
}

absl::Status IdentifierResolver::FindInParenthesizedJoin(
    const ASTParenthesizedJoin* parenthesized_join,
    const AliasSet& external_visible_aliases, AliasSet* local_visible_aliases) {
  const ASTJoin* join = parenthesized_join->join();
  // In parenthesized joins, we can't see names from outside the parentheses.
  std::unique_ptr<AliasSet> join_visible_aliases(
      new AliasSet(external_visible_aliases));
  ZETASQL_RETURN_IF_ERROR(FindInJoin(join, external_visible_aliases,
                             join_visible_aliases.get()));
  for (const std::string& alias : *join_visible_aliases) {
    zetasql_base::InsertIfNotPresent(local_visible_aliases, alias);
  }
  return absl::OkStatus();
}

absl::Status IdentifierResolver::FindInTVF(
    const ASTTVF* tvf,
    const AliasSet& external_visible_aliases, AliasSet* local_visible_aliases) {
  // The 'tvf' here is the TVF parse node. Each TVF argument may be a scalar, a
  // relation, or a TABLE clause. We've parsed all of the TVF arguments as
  // expressions by this point, so the FindInExpressionsUnder call will descend
  // into the relation arguments as expression subqueries. For TABLE clause
  // arguments, we add each named table to the set of referenced table names in
  // a separate step.
  //
  // Note about correlation: if a TVF argument is a scalar, it should resolve
  // like a correlated subquery and be able to see 'local_visible_aliases'. On
  // the other hand, if the argument is a relation, it should be uncorrelated,
  // and so those aliases should not be visible. Because we don't know whether
  // the argument should be a scalar or a relation yet, we allow correlation
  // here and examine the arguments again during resolving.
  ZETASQL_RETURN_IF_ERROR(FindInExpressionsUnder(tvf, *local_visible_aliases));
  for (const ASTTVFArgument* arg : tvf->argument_entries()) {
    if (arg->table_clause() != nullptr) {
      // Single path names are table references, to either WITH clause
      // tables or table-typed arguments to the TVF.  Multi-path names
      // cannot be related to WITH clause tables or TVF arguments, so those
      // must be table references.
      if (arg->table_clause()->table_path() != nullptr &&
          arg->table_clause()->table_path()->ToIdentifierVector() !=
              recursive_view_name_) {
        if (arg->table_clause()->table_path()->num_names() > 1) {
          zetasql_base::InsertIfNotPresent(
              table_names_,
              arg->table_clause()->table_path()->ToIdentifierVector());
        } else {
          // This is a single-part name.
          const std::string lower_name = absl::AsciiStrToLower(
              arg->table_clause()->table_path()->first_name()->GetAsString());
          if (!zetasql_base::ContainsKey(local_table_aliases_, lower_name)) {
            zetasql_base::InsertIfNotPresent(
                table_names_,
                arg->table_clause()->table_path()->ToIdentifierVector());
          }
        }
      }
      if (arg->table_clause()->tvf() != nullptr) {
        ZETASQL_RETURN_IF_ERROR(FindInTVF(arg->table_clause()->tvf(),
                                  external_visible_aliases,
                                  local_visible_aliases));
      }
    }
  }
  return absl::OkStatus();
}

absl::Status IdentifierResolver::FindInTableSubquery(
    const ASTTableSubquery* table_subquery,
    const AliasSet& external_visible_aliases,
    AliasSet* local_visible_aliases) {

  ZETASQL_RETURN_IF_ERROR(FindInQuery(table_subquery->subquery(),
                              external_visible_aliases));

  if (table_subquery->alias() != nullptr) {
    zetasql_base::InsertIfNotPresent(
        local_visible_aliases,
        absl::AsciiStrToLower(table_subquery->alias()->GetAsString()));
  }
  return absl::OkStatus();
}

absl::Status IdentifierResolver::FindInTablePathExpression(
    const ASTTablePathExpression* table_ref,
    AliasSet* visible_aliases) {

  std::string alias;
  if (table_ref->alias() != nullptr) {
    alias = table_ref->alias()->GetAsString();
  }

  if (table_ref->path_expr() != nullptr) {
    const ASTPathExpression* path_expr = table_ref->path_expr();
    std::vector<std::string> path = path_expr->ToIdentifierVector();
    ZETASQL_RET_CHECK(!path.empty());

    // Single identifiers are always table names, not range variable references,
    // but could be WITH table references or references to TVF arguments that
    // should be ignored.
    //
    // For paths, check if the first identifier is a known alias.  This allows
    // us to ignore correlated alias references like:
    //   SELECT ... FROM table AS t1, t1.arraycol
    // However, we do not want to ignore this table name if the alias matches
    // a WITH alias or TVF argument name, since multi-part table names never
    // resolve to either of these (so the full multi-part name is a reference
    // to an actual table).
    const std::string first_identifier = absl::AsciiStrToLower(path[0]);

    if ((path != recursive_view_name_) &&
        (path.size() == 1
             ? (!zetasql_base::ContainsKey(local_table_aliases_, first_identifier))
             : (!zetasql_base::ContainsKey(*visible_aliases, first_identifier)))) {
      zetasql_base::InsertIfNotPresent(table_names_, path);
      if (table_resolution_time_info_map_ != nullptr) {
        // Lookup for or insert a set of temporal expressions for 'path'.
        TableResolutionTimeInfo& temporal_expressions_set =
            (*table_resolution_time_info_map_)[path];

        const ASTForSystemTime* for_system_time = table_ref->for_system_time();
        if (for_system_time != nullptr) {
          if (!for_system_time_as_of_feature_enabled_) {
            return MakeSqlErrorAt(for_system_time)
                   << "FOR SYSTEM_TIME AS OF is not supported";
          }

          const ASTExpression* expr = for_system_time->expression();
          ZETASQL_RET_CHECK(expr != nullptr);
          std::unique_ptr<const AnalyzerOutput> analyzed;

          if (catalog_ != nullptr) {
            ZETASQL_RETURN_IF_ERROR(::zetasql::AnalyzeExpressionFromParserAST(
                *expr, *analyzer_options_, sql_, type_factory_, catalog_,
                &analyzed));
          }
          temporal_expressions_set.exprs.push_back({expr, std::move(analyzed)});
        } else {
          temporal_expressions_set.has_default_resolution_time = true;
        }
      }
    }

    if (alias.empty()) {
      alias = path.back();
    }
  }

  ZETASQL_RETURN_IF_ERROR(FindInExpressionsUnder(table_ref->unnest_expr(),
                                         *visible_aliases));

  if (!alias.empty()) {
    visible_aliases->insert(absl::AsciiStrToLower(alias));
  }

  return absl::OkStatus();
}

absl::Status IdentifierResolver::FindInExpressionsUnder(
    const ASTNode* root,
    const AliasSet& visible_aliases) {
  if (root == nullptr) return absl::OkStatus();

  // The only thing that matters inside expressions are expression subqueries,
  // which can be either ASTExpressionSubquery or ASTIn, both of which have
  // the subquery in an ASTQuery child.
  std::vector<const ASTNode*> subquery_nodes;
  root->GetDescendantSubtreesWithKinds({AST_QUERY}, &subquery_nodes);

  for (const ASTNode* subquery_node : subquery_nodes) {
    ZETASQL_RETURN_IF_ERROR(FindInQuery(subquery_node->GetAs<ASTQuery>(),
                                visible_aliases));
  }

  return absl::OkStatus();
}

absl::Status IdentifierResolver::FindInOptionsListUnder(
    const ASTNode* root,
    const AliasSet& visible_aliases) {
  if (root == nullptr) return absl::OkStatus();

  std::vector<const ASTNode*> options_list_nodes;
  root->GetDescendantSubtreesWithKinds({AST_OPTIONS_LIST}, &options_list_nodes);

  for (const ASTNode* options_list : options_list_nodes) {
    ZETASQL_RETURN_IF_ERROR(FindInExpressionsUnder(options_list, visible_aliases));
  }
  return absl::OkStatus();
}
}  // namespace

zetasql_base::StatusOr<std::map<ResolvedNodeKind, TableNamesSet>> GetNodeKindToTableNamesMap(
    const std::string& sql_file_path, const AnalyzerOptions& analyzer_options, TableNamesSet* table_names) {
  std::unique_ptr<ParserOutput> parser_output;
  // AnalyzerOptions local_options = analyzer_options;

  std::filesystem::path file_path(sql_file_path);
  std::ifstream file(file_path, std::ios::in);
  std::string sql(std::istreambuf_iterator<char>(file), {});

  ParseResumeLocation location = ParseResumeLocation::FromStringView(sql_file_path, sql);
  bool at_end_of_input = false;

  auto resolver = IdentifierResolver(sql, &analyzer_options, nullptr, nullptr,
                                    table_names, nullptr);

  while (!at_end_of_input) {
    auto status = ParseNextStatement(&location, analyzer_options.GetParserOptions(),
                                     &parser_output, &at_end_of_input);

    if (parser_output == nullptr) {
      return ConvertInternalErrorLocationAndAdjustErrorString(
          analyzer_options.error_message_mode(), sql, status);
    }

    status = resolver.FindInStatement(parser_output->statement());
    if (!status.ok()) {
      return ConvertInternalErrorLocationAndAdjustErrorString(
          analyzer_options.error_message_mode(), sql, status);
    }
  }

  return resolver.node_kind_to_table_names();
}

absl::Status FindTableNamesInScript(absl::string_view sql,
                                    const ASTScript& script,
                                    const AnalyzerOptions& analyzer_options,
                                    TableNamesSet* table_names) {
  return IdentifierResolver(sql, &analyzer_options, /*type_factory=*/nullptr,
                           /*catalog=*/nullptr, table_names,
                           /*table_resolution_time_info_map=*/nullptr)
      .FindTableNames(script);

}

}  // namespace table_name_resolver
}  // namespace zetasql