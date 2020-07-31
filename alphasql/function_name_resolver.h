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
  void visitASTFunctionCall(const ASTFunctionCall* node, void* data);
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
}  // namespace function_name_resolver

#endif  // ALPHASQL_FUNCTION_NAME_RESOLVER_H_
