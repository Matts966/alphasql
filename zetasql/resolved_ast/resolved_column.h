//
// Copyright 2019 ZetaSQL Authors
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

#ifndef ZETASQL_RESOLVED_AST_RESOLVED_COLUMN_H_
#define ZETASQL_RESOLVED_AST_RESOLVED_COLUMN_H_

#include <stddef.h>
#include <string>
#include <vector>

#include "zetasql/base/logging.h"
#include "zetasql/public/id_string.h"
#include "zetasql/public/type.h"
#include "zetasql/resolved_ast/resolved_node.h"
#include "zetasql/resolved_ast/serialization.pb.h"
#include "zetasql/base/status.h"
#include "zetasql/base/statusor.h"

namespace zetasql {

// A column produced by part of a query (e.g. a scan or subquery).
//
// This is used in the column_list of a resolved AST node to represent a
// "slot" in the "tuple" produced by that logical operator.
// This is also used in expressions in ResolvedColumnRef to point at the
// column selected during name resolution.
//
// The column_id is the definitive identifier for a ResolvedColumn, and
// the column_id should be used to match a ResolvedColumnRef in an expression
// to the scan that produces that column.  column_ids are unique within
// a query.  If the same table is scanned multiple times, distinct column_ids
// will be chosen for each scan.
//
// Joins and other combining nodes may propagate ResolvedColumns from their
// inputs, with the same column_ids.
class ResolvedColumn {
 public:
  // Default constructor makes an uninitialized ResolvedColumn.
  ResolvedColumn() = default;
  ResolvedColumn(const ResolvedColumn&) = default;

  // Construct a ResolvedColumn with the given <column_id> and <type>.
  // <table_name> and <name> are for display only, have no defined meaning and
  // are required to be non-empty.
  //
  // NOTE: The IdString constructor is preferred because it avoids doing any
  // string copying.  We don't want the string-based constructor to be called
  // anywhere during zetasql analysis, but there are outside callers.
  // WARNING: This string-based constructor allocates IdStrings in the
  // global IdStringPool, so they never get freed.  Avoid using this for an
  // unbounded number of strings.
  // TODO Maybe get this removed, or figure out a way to enforce that
  // zetasql code can't call it.
  ResolvedColumn(int column_id, const std::string& table_name,
                 const std::string& name, const Type* type);
  ResolvedColumn(int column_id, IdString table_name,
                 IdString name, const Type* type)
      : column_id_(column_id), table_name_(table_name),
        name_(name), type_(type) {
    DCHECK_GT(column_id, 0) << "column_id should be positive";
    DCHECK(!table_name.empty());
    DCHECK(!name.empty());
    DCHECK(type != nullptr) << "table_id: " << table_name << ", column_id: " << name;
  }

  // Return true if this ResolvedColumn has been initialized.
  bool IsInitialized() const { return column_id_ > 0; }

  // Reset this object so IsInitialized returns false.
  void Clear() {
    column_id_ = -1;
    table_name_.clear();
    name_.clear();
    type_ = nullptr;
  }

  // Return "<table>.<column>#<column_id>".
  std::string DebugString() const;

  // Return "<column>#<column_id>".
  std::string ShortDebugString() const;

  absl::Status SaveTo(
      FileDescriptorSetMap* file_descriptor_set_map,
      ResolvedColumnProto* proto) const;

  static zetasql_base::StatusOr<ResolvedColumn> RestoreFrom(
      const ResolvedColumnProto& proto,
      const ResolvedNode::RestoreParams& params);

  int column_id() const { return column_id_; }

  // Get the table and column name.  The _id forms return an IdString so
  // do not have to copy a string.  The non-_id forms are slower and should
  // not be used in zetasql analysis code.
  //
  // <table_name> and <name> are for display only, have no defined meaning and
  // are required to be non-empty.  Semantic behavior must never be defined
  // using these names.
  const std::string table_name() const { return table_name_.ToString(); }
  const std::string name() const { return name_.ToString(); }
  IdString table_name_id() const { return table_name_; }
  IdString name_id() const { return name_; }

  const Type* type() const { return type_; }

  // Equality is defined using column_id only.
  bool operator==(const ResolvedColumn& other) const {
    return column_id_ == other.column_id_;
  }

  // Order by column_id, so these can be stored in a set.
  bool operator<(const ResolvedColumn& other) const {
    return column_id_ < other.column_id_;
  }

  template <typename H>
  friend H AbslHashValue(H h, const ResolvedColumn& c) {
    return H::combine(std::move(h), c.column_id_);
  }

 private:
  // Globally unique ID (within one query) for this ResolvedColumn.
  // Always positive if valid.
  int column_id_ = -1;

  // Table name (or alias) this column comes from.
  // Not necessarily unique or meaningful - used only for DebugString to
  // make the resolved AST understandable when printed.
  IdString table_name_;

  // The name of this column.  Matches the identifier that could have been
  // used to select this column (if it was properly scoped).
  IdString name_;

  // The type of this column.  Not owned.
  const Type* type_ = nullptr;
};

// A vector of columns produced by an operation like a scan or subquery.
// This effectively describes the "tuple" output row that comes out of a
// logical operation.
typedef std::vector<ResolvedColumn> ResolvedColumnList;

// Return a formatted ResolvedColumnList.
// Uses an abbreviated output format if all columns have the same table_name.
std::string ResolvedColumnListToString(const ResolvedColumnList& columns);

// DEPRECATED: Use absl::Hash<ResolvedColumn> directly instead.
using ResolvedColumnHasher = absl::Hash<ResolvedColumn>;

}  // namespace zetasql

#endif  // ZETASQL_RESOLVED_AST_RESOLVED_COLUMN_H_
