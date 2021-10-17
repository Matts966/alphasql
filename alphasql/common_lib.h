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

#include "zetasql/parser/bison_parser.h"
#include "zetasql/parser/bison_parser_mode.h"
#include "zetasql/parser/parse_tree.h"
#include "zetasql/parser/parser.h"

namespace zetasql {

using namespace zetasql::parser;
using zetasql::parser::BisonParser;
using zetasql::parser::BisonParserMode;

absl::Status ParseScript(absl::string_view script_string,
                         const ParserOptions &parser_options_in,
                         ErrorMessageMode error_message_mode,
                         std::unique_ptr<ParserOutput> *output,
                         const std::string &filename) {
  ParserOptions parser_options = parser_options_in;
  parser_options.CreateDefaultArenasIfNotSet();

  BisonParser parser;
  std::unique_ptr<ASTNode> ast_node;
  std::vector<std::unique_ptr<ASTNode>> other_allocated_ast_nodes;
  absl::Status status = parser.Parse(
      BisonParserMode::kScript, filename, script_string,
      /*start_byte_offset=*/0, parser_options.id_string_pool().get(),
      parser_options.arena().get(), parser_options.language_options(),
      &ast_node, &other_allocated_ast_nodes,
      /*ast_statement_properties=*/nullptr,
      /*statement_end_byte_offset=*/nullptr);

  std::unique_ptr<ASTScript> script;
  if (status.ok()) {
    ZETASQL_RET_CHECK_EQ(ast_node->node_kind(), AST_SCRIPT);
    script = absl::WrapUnique(ast_node.release()->GetAsOrDie<ASTScript>());
  }
  ZETASQL_RETURN_IF_ERROR(ConvertInternalErrorLocationAndAdjustErrorString(
      error_message_mode, script_string, status));
  *output = absl::make_unique<ParserOutput>(
      parser_options.id_string_pool(), parser_options.arena(),
      std::move(other_allocated_ast_nodes), std::move(script));
  return absl::OkStatus();
}

} // namespace alphasql
