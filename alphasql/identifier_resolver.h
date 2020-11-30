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

#ifndef ALPHASQL_IDENTIFIER_RESOLVER_H_
#define ALPHASQL_IDENTIFIER_RESOLVER_H_

#include <string>
#include <filesystem>

#include "zetasql/base/logging.h"
#include "zetasql/parser/parse_tree.h"
#include "zetasql/parser/parse_tree_visitor.h"
#include "zetasql/public/analyzer.h"
#include "absl/flags/flag.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

ABSL_DECLARE_FLAG(bool, warning_as_error);

namespace alphasql {

using namespace zetasql::parser;
using namespace zetasql;

const AnalyzerOptions GetAnalyzerOptions();

namespace identifier_resolver {

struct table_info {
  std::set<std::vector<std::string>> created;
  std::set<std::vector<std::string>> referenced;
  std::set<std::vector<std::string>> dropped;
};

struct function_info {
  std::set<std::vector<std::string>> called;
  std::set<std::vector<std::string>> defined;
  std::set<std::vector<std::string>> dropped;
};

struct identifier_info {
  function_info function_information;
  table_info table_information;
};

zetasql_base::StatusOr<identifier_info> GetIdentifierInformation(const std::string& sql_file_path);

class IdentifierResolver : public DefaultParseTreeVisitor {
 public:
  explicit IdentifierResolver() {}
  IdentifierResolver(const IdentifierResolver&) = delete;
  IdentifierResolver& operator=(const IdentifierResolver&) = delete;
  ~IdentifierResolver() override {}

  identifier_info identifier_information;
  std::set<std::string> temporary_tables;

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
  // Tables
  void visitASTDropStatement(const ASTDropStatement* node, void* data) override;
  void visitASTCreateTableStatement(const ASTCreateTableStatement* node,
                                    void* data) override;
  void visitASTInsertStatement(const ASTInsertStatement* node, void* data) override;
  void visitASTUpdateStatement(const ASTUpdateStatement* node, void* data) override;

  // Functions
  void visitASTDropFunctionStatement(
      const ASTDropFunctionStatement* node, void* data) override;
  void visitASTFunctionCall(const ASTFunctionCall* node, void* data) override;
  void visitASTFunctionDeclaration(
      const ASTFunctionDeclaration* node, void* data) override;
  void visitASTCreateFunctionStatement(const ASTCreateFunctionStatement* node,
                                       void* data) override;
  void visitASTCreateTableFunctionStatement(
      const ASTCreateTableFunctionStatement* node, void* data) override;
  void visitASTCallStatement(const ASTCallStatement* node,
                             void* data) override;
};

}  // namespace alphasql
}  // namespace identifier_resolver

#endif  // ALPHASQL_identifier_resolver_H_
