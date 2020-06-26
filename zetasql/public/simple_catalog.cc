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

#include "zetasql/public/simple_catalog.h"

#include <map>
#include <memory>
#include <vector>

#include "zetasql/base/logging.h"
#include "zetasql/proto/simple_catalog.pb.h"
#include "zetasql/public/constant.h"
#include "zetasql/public/procedure.h"
#include "zetasql/public/simple_constant.pb.h"
#include "zetasql/public/simple_table.pb.h"
#include "zetasql/public/strings.h"
#include "zetasql/public/table_valued_function.h"
#include "absl/memory/memory.h"
#include "absl/strings/ascii.h"
#include "zetasql/base/case.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "zetasql/base/map_util.h"
#include "zetasql/base/source_location.h"
#include "zetasql/base/ret_check.h"
#include "zetasql/base/status.h"
#include "zetasql/base/status_macros.h"
#include "zetasql/base/statusor.h"

namespace zetasql {

SimpleCatalog::SimpleCatalog(const std::string& name, TypeFactory* type_factory)
    : name_(name), type_factory_(type_factory) {}

absl::Status SimpleCatalog::GetTable(const std::string& name,
                                     const Table** table,
                                     const FindOptions& options) {
  absl::MutexLock l(&mutex_);
  *table = zetasql_base::FindPtrOrNull(tables_, absl::AsciiStrToLower(name));
  return absl::OkStatus();
}

absl::Status SimpleCatalog::GetModel(const std::string& name,
                                     const Model** model,
                                     const FindOptions& options) {
  absl::MutexLock l(&mutex_);
  *model = zetasql_base::FindPtrOrNull(models_, absl::AsciiStrToLower(name));
  return absl::OkStatus();
}

absl::Status SimpleCatalog::GetConnection(const std::string& name,
                                          const Connection** connection,
                                          const FindOptions& options) {
  absl::MutexLock l(&mutex_);
  *connection = zetasql_base::FindPtrOrNull(connections_, absl::AsciiStrToLower(name));
  return absl::OkStatus();
}

absl::Status SimpleCatalog::GetFunction(const std::string& name,
                                        const Function** function,
                                        const FindOptions& options) {
  absl::MutexLock l(&mutex_);
  *function = zetasql_base::FindPtrOrNull(functions_, absl::AsciiStrToLower(name));
  return absl::OkStatus();
}

absl::Status SimpleCatalog::GetTableValuedFunction(
    const std::string& name, const TableValuedFunction** function,
    const FindOptions& options) {
  absl::MutexLock l(&mutex_);
  *function =
      zetasql_base::FindPtrOrNull(table_valued_functions_, absl::AsciiStrToLower(name));
  return absl::OkStatus();
}

absl::Status SimpleCatalog::GetProcedure(const std::string& name,
                                         const Procedure** procedure,
                                         const FindOptions& options) {
  absl::MutexLock l(&mutex_);
  *procedure = zetasql_base::FindPtrOrNull(procedures_, absl::AsciiStrToLower(name));
  return absl::OkStatus();
}

absl::Status SimpleCatalog::GetType(const std::string& name, const Type** type,
                                    const FindOptions& options) {
  const google::protobuf::DescriptorPool* pool;
  {
    absl::MutexLock l(&mutex_);
    // Types contained in types_ have case-insensitive names, so we lowercase
    // the name as is done in AddType.
    *type = zetasql_base::FindPtrOrNull(types_, absl::AsciiStrToLower(name));
    if (*type != nullptr) {
      return absl::OkStatus();
    }
    // Avoid holding the mutex while calling descriptor_pool_ methods.
    // descriptor_pool_ is const once it has been set.
    pool = descriptor_pool_;
  }

  if (pool != nullptr) {
    const google::protobuf::Descriptor* descriptor = pool->FindMessageTypeByName(name);
    if (descriptor != nullptr) {
      return type_factory()->MakeProtoType(descriptor, type);
    }
    const google::protobuf::EnumDescriptor* enum_descriptor =
        pool->FindEnumTypeByName(name);
    if (enum_descriptor != nullptr) {
      return type_factory()->MakeEnumType(enum_descriptor, type);
    }
  }

  DCHECK(*type == nullptr);
  return absl::OkStatus();
}

absl::Status SimpleCatalog::GetCatalog(const std::string& name,
                                       Catalog** catalog,
                                       const FindOptions& options) {
  absl::MutexLock l(&mutex_);
  *catalog = zetasql_base::FindPtrOrNull(catalogs_, absl::AsciiStrToLower(name));
  return absl::OkStatus();
}

absl::Status SimpleCatalog::GetConstant(const std::string& name,
                                        const Constant** constant,
                                        const FindOptions& options) {
  absl::MutexLock l(&mutex_);
  *constant = zetasql_base::FindPtrOrNull(constants_, absl::AsciiStrToLower(name));
  return absl::OkStatus();
}

std::string SimpleCatalog::SuggestTable(
    const absl::Span<const std::string>& mistyped_path) {
  if (mistyped_path.empty()) {
    // Nothing to suggest here.
    return "";
  }

  const std::string& name = mistyped_path.front();
  if (mistyped_path.length() > 1) {
    Catalog* catalog = nullptr;
    if (GetCatalog(name, &catalog).ok() && catalog != nullptr) {
      absl::Span<const std::string> mistyped_path_suffix =
          mistyped_path.subspan(1, mistyped_path.length() - 1);
      const std::string closest_name =
          catalog->SuggestTable(mistyped_path_suffix);
      if (!closest_name.empty()) {
        return absl::StrCat(catalog->FullName(), ".", closest_name);
      }
    }
  } else {
    const FindOptions& find_options = FindOptions();
    const Table* table = nullptr;
    if (FindTable({name}, &table, find_options).ok()) {
      return table->Name();
    }

    std::vector<Catalog*> sub_catalogs = catalogs();
    std::string closest_name;
    for (int i = 0; i < sub_catalogs.size(); ++i) {
      if ((sub_catalogs[i]->FindTable({name}, &table, find_options).ok())) {
        const std::string result =
            absl::StrCat(sub_catalogs[i]->FullName(), ".", table->Name());
        // We choose the name which occurs lexicographically first to keep the
        // result deterministic and independent of the order of sub-catalogs.
        if (closest_name.empty() || closest_name.compare(result) > 0) {
          closest_name = result;
        }
      }
    }
    if (!closest_name.empty()) {
      return closest_name;
    }
  }

  // No suggestion obtained.
  return "";
}

std::string SimpleCatalog::SuggestFunctionOrTableValuedFunction(
    bool is_table_valued_function,
    absl::Span<const std::string> mistyped_path) {
  if (mistyped_path.empty()) {
    // Nothing to suggest here.
    return "";
  }

  const std::string& name = mistyped_path.front();
  if (mistyped_path.length() > 1) {
    Catalog* catalog = nullptr;
    if (GetCatalog(name, &catalog).ok() && catalog != nullptr) {
      absl::Span<const std::string> path_suffix =
          mistyped_path.subspan(1, mistyped_path.length() - 1);
      const std::string closest_name =
          is_table_valued_function
              ? catalog->SuggestTableValuedFunction(path_suffix)
              : catalog->SuggestFunction(path_suffix);
      if (!closest_name.empty()) {
        return absl::StrCat(catalog->FullName(), ".", closest_name);
      }
    }
  } else {
  }

  // No suggestion obtained.
  return "";
}

std::string SimpleCatalog::SuggestFunction(
    const absl::Span<const std::string>& mistyped_path) {
  return SuggestFunctionOrTableValuedFunction(
      /*is_table_valued_function=*/false, mistyped_path);
}

std::string SimpleCatalog::SuggestTableValuedFunction(
    const absl::Span<const std::string>& mistyped_path) {
  return SuggestFunctionOrTableValuedFunction(
      /*is_table_valued_function=*/true, mistyped_path);
}

std::string SimpleCatalog::SuggestConstant(
    const absl::Span<const std::string>& mistyped_path) {
  if (mistyped_path.empty()) {
    // Nothing to suggest here.
    return "";
  }

  const std::string& name = mistyped_path.front();
  if (mistyped_path.length() > 1) {
    Catalog* catalog = nullptr;
    if (GetCatalog(name, &catalog).ok() && catalog != nullptr) {
      const std::string closest_name = catalog->SuggestConstant(
          mistyped_path.subspan(1, mistyped_path.length() - 1));
      if (!closest_name.empty()) {
        return absl::StrCat(ToIdentifierLiteral(catalog->FullName()), ".",
                            closest_name);
      }
    }
  } else {
  }

  // No suggestion obtained.
  return "";
}

void SimpleCatalog::AddTable(const std::string& name, const Table* table) {
  absl::MutexLock l(&mutex_);
  zetasql_base::InsertOrDie(&tables_, absl::AsciiStrToLower(name), table);
}

void SimpleCatalog::AddModel(const std::string& name, const Model* model) {
  absl::MutexLock l(&mutex_);
  zetasql_base::InsertOrDie(&models_, absl::AsciiStrToLower(name), model);
}

void SimpleCatalog::AddConnection(const std::string& name,
                                  const Connection* connection) {
  absl::MutexLock l(&mutex_);
  zetasql_base::InsertOrDie(&connections_, absl::AsciiStrToLower(name), connection);
}

void SimpleCatalog::AddType(const std::string& name, const Type* type) {
  absl::MutexLock l(&mutex_);
  AddTypeLocked(name, type);
}

void SimpleCatalog::AddTypeLocked(const std::string& name, const Type* type) {
  zetasql_base::InsertOrDie(&types_, absl::AsciiStrToLower(name), type);
}

void SimpleCatalog::AddCatalog(const std::string& name, Catalog* catalog) {
  absl::MutexLock l(&mutex_);
  AddCatalogLocked(name, catalog);
}

void SimpleCatalog::AddCatalogLocked(const std::string& name,
                                     Catalog* catalog) {
  zetasql_base::InsertOrDie(&catalogs_, absl::AsciiStrToLower(name), catalog);
}

void SimpleCatalog::AddFunctionLocked(const std::string& name,
                                      const Function* function) {
  zetasql_base::InsertOrDie(&functions_, absl::AsciiStrToLower(name), function);
  if (!function->alias_name().empty() &&
      zetasql_base::StringCaseCompare(function->alias_name(), name) != 0) {
    zetasql_base::InsertOrDie(&functions_, absl::AsciiStrToLower(function->alias_name()),
                     function);
  }
}

void SimpleCatalog::AddFunction(const std::string& name,
                                const Function* function) {
  absl::MutexLock l(&mutex_);
  AddFunctionLocked(name, function);
}

void SimpleCatalog::AddTableValuedFunctionLocked(
    const std::string& name, const TableValuedFunction* table_function) {
  zetasql_base::InsertOrDie(&table_valued_functions_, absl::AsciiStrToLower(name),
                   table_function);
}

void SimpleCatalog::AddTableValuedFunction(
    const std::string& name, const TableValuedFunction* function) {
  absl::MutexLock l(&mutex_);
  AddTableValuedFunctionLocked(name, function);
}

void SimpleCatalog::AddProcedure(const std::string& name,
                                 const Procedure* procedure) {
  absl::MutexLock l(&mutex_);
  zetasql_base::InsertOrDie(&procedures_, absl::AsciiStrToLower(name), procedure);
}

void SimpleCatalog::AddConstant(const std::string& name,
                                const Constant* constant) {
  absl::MutexLock l(&mutex_);
  AddConstantLocked(name, constant);
}

void SimpleCatalog::AddConstantLocked(const std::string& name,
                                      const Constant* constant) {
  zetasql_base::InsertOrDie(&constants_, absl::AsciiStrToLower(name), constant);
}

void SimpleCatalog::AddOwnedTable(const std::string& name,
                                  std::unique_ptr<const Table> table) {
  AddTable(name, table.get());
  absl::MutexLock l(&mutex_);
  owned_tables_.push_back(std::move(table));
}

bool SimpleCatalog::AddOwnedTableIfNotPresent(
    const std::string& name, std::unique_ptr<const Table> table) {
  absl::MutexLock l(&mutex_);
  if (!zetasql_base::InsertIfNotPresent(&tables_, absl::AsciiStrToLower(name),
                               table.get())) {
    return false;
  }
  owned_tables_.emplace_back(std::move(table));
  return true;
}

void SimpleCatalog::AddOwnedTable(const std::string& name, const Table* table) {
  AddOwnedTable(name, absl::WrapUnique(table));
}

void SimpleCatalog::DropOwnedTable(const std::string& name) {
  absl::MutexLock l(&mutex_);

  tables_.erase(absl::AsciiStrToLower(name));

  for (auto it = owned_tables_.begin(); it != owned_tables_.end();) {
    if (it->get()->Name() == name) {
      it = owned_tables_.erase(it);
      return;
    } else {
      ++it;
    }
  }
  CHECK(false) << "No table named " << name;
}

void SimpleCatalog::DropOwnedTableIfExists(const std::string& name) {
  absl::MutexLock l(&mutex_);

  tables_.erase(absl::AsciiStrToLower(name));

  for (auto it = owned_tables_.begin(); it != owned_tables_.end();) {
    if (it->get()->Name() == name) {
      it = owned_tables_.erase(it);
      return;
    } else {
      ++it;
    }
  }
}

void SimpleCatalog::AddOwnedModel(const std::string& name,
                                  std::unique_ptr<const Model> model) {
  AddModel(name, model.get());
  absl::MutexLock l(&mutex_);
  owned_models_.emplace_back(std::move(model));
}

void SimpleCatalog::AddOwnedModel(const std::string& name, const Model* model) {
  AddOwnedModel(name, absl::WrapUnique(model));
}

void SimpleCatalog::AddOwnedCatalog(const std::string& name,
                                    std::unique_ptr<Catalog> catalog) {
  AddCatalog(name, catalog.get());
  absl::MutexLock l(&mutex_);
  owned_catalogs_.push_back(std::move(catalog));
}

void SimpleCatalog::AddOwnedCatalog(const std::string& name, Catalog* catalog) {
  AddOwnedCatalog(name, absl::WrapUnique(catalog));
}

void SimpleCatalog::AddOwnedFunction(const std::string& name,
                                     std::unique_ptr<const Function> function) {
  absl::MutexLock l(&mutex_);
  AddOwnedFunctionLocked(name, std::move(function));
}

void SimpleCatalog::AddOwnedFunction(const std::string& name,
                                     const Function* function) {
  AddOwnedFunction(name, absl::WrapUnique(function));
}

void SimpleCatalog::DropOwnedFunction(const std::string& full_name_without_group) {
  absl::MutexLock l(&mutex_);

  functions_.erase(absl::AsciiStrToLower(full_name_without_group));

  for (auto it = owned_functions_.begin(); it != owned_functions_.end();) {
    if (it->get()->FullName(false /* include_group */) == full_name_without_group) {
      it = owned_functions_.erase(it);
      return;
    } else {
      ++it;
    }
  }
  CHECK(false) << "No function named " << full_name_without_group;
}

void SimpleCatalog::AddOwnedFunctionLocked(
    const std::string& name, std::unique_ptr<const Function> function) {
  AddFunctionLocked(name, function.get());
  owned_functions_.emplace_back(std::move(function));
}

void SimpleCatalog::AddOwnedTableValuedFunction(
    const std::string& name,
    std::unique_ptr<const TableValuedFunction> function) {
  AddTableValuedFunction(name, function.get());
  absl::MutexLock l(&mutex_);
  owned_table_valued_functions_.emplace_back(std::move(function));
}

void SimpleCatalog::AddOwnedTableValuedFunction(
    const std::string& name, const TableValuedFunction* function) {
  AddOwnedTableValuedFunction(name, absl::WrapUnique(function));
}

void SimpleCatalog::AddOwnedTableValuedFunctionLocked(
    const std::string& name,
    std::unique_ptr<const TableValuedFunction> table_function) {
  AddTableValuedFunctionLocked(name, table_function.get());
  owned_table_valued_functions_.emplace_back(std::move(table_function));
}

void SimpleCatalog::AddOwnedProcedure(
    const std::string& name, std::unique_ptr<const Procedure> procedure) {
  AddProcedure(name, procedure.get());
  absl::MutexLock l(&mutex_);
  owned_procedures_.push_back(std::move(procedure));
}

bool SimpleCatalog::AddOwnedProcedureIfNotPresent(
    std::unique_ptr<Procedure> procedure) {
  absl::MutexLock l(&mutex_);
  if (!zetasql_base::InsertIfNotPresent(&procedures_,
                               absl::AsciiStrToLower(procedure->Name()),
                               procedure.get())) {
    return false;
  }
  owned_procedures_.emplace_back(std::move(procedure));
  return true;
}

void SimpleCatalog::AddOwnedProcedure(const std::string& name,
                                      const Procedure* procedure) {
  AddOwnedProcedure(name, absl::WrapUnique(procedure));
}

void SimpleCatalog::AddOwnedConstant(const std::string& name,
                                     std::unique_ptr<const Constant> constant) {
  AddConstant(name, constant.get());
  absl::MutexLock l(&mutex_);
  owned_constants_.push_back(std::move(constant));
}

void SimpleCatalog::AddOwnedConstant(const std::string& name,
                                     const Constant* constant) {
  AddOwnedConstant(name, absl::WrapUnique(constant));
}

void SimpleCatalog::AddTable(const Table* table) {
  AddTable(table->Name(), table);
}

void SimpleCatalog::AddModel(const Model* model) {
  AddModel(model->Name(), model);
}

void SimpleCatalog::AddConnection(const Connection* connection) {
  AddConnection(connection->Name(), connection);
}

void SimpleCatalog::AddCatalog(Catalog* catalog) {
  AddCatalog(catalog->FullName(), catalog);
}

void SimpleCatalog::AddFunction(const Function* function) {
  AddFunction(function->Name(), function);
}

void SimpleCatalog::AddTableValuedFunction(
    const TableValuedFunction* function) {
  AddTableValuedFunction(function->Name(), function);
}

void SimpleCatalog::AddProcedure(const Procedure* procedure) {
  AddProcedure(procedure->Name(), procedure);
}

void SimpleCatalog::AddConstant(const Constant* constant) {
  AddConstant(constant->Name(), constant);
}

void SimpleCatalog::AddOwnedTable(std::unique_ptr<const Table> table) {
  AddTable(table.get());
  absl::MutexLock l(&mutex_);
  owned_tables_.push_back(std::move(table));
}

void SimpleCatalog::AddOwnedTable(const Table* table) {
  AddOwnedTable(absl::WrapUnique(table));
}

void SimpleCatalog::AddOwnedModel(std::unique_ptr<const Model> model) {
  AddModel(model.get());
  absl::MutexLock l(&mutex_);
  owned_models_.emplace_back(std::move(model));
}

void SimpleCatalog::AddOwnedModel(const Model* model) {
  AddOwnedModel(absl::WrapUnique(model));
}

void SimpleCatalog::AddOwnedCatalog(std::unique_ptr<Catalog> catalog) {
  absl::MutexLock l(&mutex_);
  const std::string name = catalog->FullName();
  AddOwnedCatalogLocked(name, std::move(catalog));
}

void SimpleCatalog::AddOwnedCatalog(Catalog* catalog) {
  AddOwnedCatalog(absl::WrapUnique(catalog));
}

void SimpleCatalog::AddOwnedCatalogLocked(const std::string& name,
                                          std::unique_ptr<Catalog> catalog) {
  AddCatalogLocked(name, catalog.get());
  owned_catalogs_.emplace_back(std::move(catalog));
}

bool SimpleCatalog::AddOwnedCatalogIfNotPresent(
    const std::string& name, std::unique_ptr<Catalog> catalog) {
  absl::MutexLock l(&mutex_);
  if (zetasql_base::ContainsKey(catalogs_, absl::AsciiStrToLower(name))) {
    return false;
  }
  AddOwnedCatalogLocked(name, std::move(catalog));
  return true;
}

void SimpleCatalog::AddOwnedFunction(std::unique_ptr<const Function> function) {
  AddFunction(function->Name(), function.get());
  absl::MutexLock l(&mutex_);
  owned_functions_.push_back(std::move(function));
}

void SimpleCatalog::AddOwnedFunction(const Function* function) {
  AddOwnedFunction(function->Name(), absl::WrapUnique(function));
}

bool SimpleCatalog::AddOwnedFunctionIfNotPresent(
    const std::string& name, std::unique_ptr<Function>* function) {
  absl::MutexLock l(&mutex_);
  // If the function name exists, return false.
  if (zetasql_base::ContainsKey(functions_, absl::AsciiStrToLower(name))) {
    return false;
  }
  const std::string alias_name = (*function)->alias_name();
  // If the function has an alias and the alias exists, return false.
  if (!alias_name.empty() && zetasql_base::StringCaseCompare(alias_name, name) != 0) {
    if (zetasql_base::ContainsKey(functions_, absl::AsciiStrToLower(alias_name))) {
      return false;
    }
  }
  AddOwnedFunctionLocked(name, std::move(*function));
  return true;
}

bool SimpleCatalog::AddOwnedFunctionIfNotPresent(
    std::unique_ptr<Function>* function) {
  return AddOwnedFunctionIfNotPresent((*function)->Name(), function);
}

void SimpleCatalog::AddOwnedTableValuedFunction(
    std::unique_ptr<const TableValuedFunction> function) {
  AddTableValuedFunction(function.get());
  absl::MutexLock l(&mutex_);
  owned_table_valued_functions_.push_back(std::move(function));
}

void SimpleCatalog::AddOwnedTableValuedFunction(
    const TableValuedFunction* function) {
  AddOwnedTableValuedFunction(absl::WrapUnique(function));
}

bool SimpleCatalog::AddOwnedTableValuedFunctionIfNotPresent(
    const std::string& name,
    std::unique_ptr<TableValuedFunction>* table_function) {
  absl::MutexLock l(&mutex_);
  // If the table function name exists, return false.
  if (zetasql_base::ContainsKey(table_valued_functions_, absl::AsciiStrToLower(name))) {
    return false;
  }
  AddOwnedTableValuedFunctionLocked(name, std::move(*table_function));
  return true;
}

bool SimpleCatalog::AddOwnedTableValuedFunctionIfNotPresent(
    std::unique_ptr<TableValuedFunction>* table_function) {
  return AddOwnedTableValuedFunctionIfNotPresent((*table_function)->Name(),
                                                 table_function);
}

bool SimpleCatalog::AddTypeIfNotPresent(const std::string& name,
                                        const Type* type) {
  absl::MutexLock l(&mutex_);
  // If the table function name exists, return false.
  if (zetasql_base::ContainsKey(types_, absl::AsciiStrToLower(name))) {
    return false;
  }
  AddTypeLocked(name, type);
  return true;
}

void SimpleCatalog::AddOwnedProcedure(
    std::unique_ptr<const Procedure> procedure) {
  AddProcedure(procedure.get());
  absl::MutexLock l(&mutex_);
  owned_procedures_.emplace_back(std::move(procedure));
}

void SimpleCatalog::AddOwnedProcedure(const Procedure* procedure) {
  AddOwnedProcedure(absl::WrapUnique(procedure));
}

void SimpleCatalog::AddOwnedConstant(std::unique_ptr<const Constant> constant) {
  absl::MutexLock l(&mutex_);
  AddConstantLocked(constant->Name(), constant.get());
  owned_constants_.push_back(std::move(constant));
}

bool SimpleCatalog::AddOwnedConstantIfNotPresent(
    std::unique_ptr<const Constant> constant) {
  absl::MutexLock l(&mutex_);
  if (!zetasql_base::InsertIfNotPresent(&constants_,
                               absl::AsciiStrToLower(constant->Name()),
                               constant.get())) {
    return false;
  }
  owned_constants_.push_back(std::move(constant));
  return true;
}

void SimpleCatalog::AddOwnedConstant(const Constant* constant) {
  AddOwnedConstant(absl::WrapUnique(constant));
}

SimpleCatalog* SimpleCatalog::MakeOwnedSimpleCatalog(const std::string& name) {
  SimpleCatalog* new_catalog = new SimpleCatalog(name, type_factory());
  AddOwnedCatalog(new_catalog);
  return new_catalog;
}

void SimpleCatalog::SetDescriptorPool(const google::protobuf::DescriptorPool* pool) {
  absl::MutexLock l(&mutex_);
  CHECK(descriptor_pool_ == nullptr)
      << "SimpleCatalog::SetDescriptorPool can only be called once";
  owned_descriptor_pool_.reset();
  descriptor_pool_ = pool;
}

void SimpleCatalog::SetOwnedDescriptorPool(const google::protobuf::DescriptorPool* pool) {
  absl::MutexLock l(&mutex_);
  CHECK(descriptor_pool_ == nullptr)
      << "SimpleCatalog::SetDescriptorPool can only be called once";
  owned_descriptor_pool_.reset(pool);
  descriptor_pool_ = pool;
}

void SimpleCatalog::AddZetaSQLFunctions(
    const ZetaSQLBuiltinFunctionOptions& options) {
  std::map<std::string, std::unique_ptr<Function>> function_map;
  // We have to call type_factory() while not holding mutex_.
  TypeFactory* type_factory = this->type_factory();
  GetZetaSQLFunctions(type_factory, options, &function_map);
  for (auto& function_pair : function_map) {
    const std::vector<std::string>& path =
        function_pair.second->FunctionNamePath();
    SimpleCatalog* catalog = this;
    if (path.size() > 1) {
      CHECK_LE(path.size(), 2);
      absl::MutexLock l(&mutex_);
      const std::string& space = path[0];
      auto sub_entry = owned_zetasql_subcatalogs_.find(space);
      if (sub_entry != owned_zetasql_subcatalogs_.end()) {
        catalog = sub_entry->second.get();
        CHECK(catalog != nullptr) << "internal state corrupt: " << space;
      } else {
        auto new_catalog =
            absl::make_unique<SimpleCatalog>(space, type_factory);
        AddCatalogLocked(space, new_catalog.get());
        catalog = new_catalog.get();
        CHECK(
            owned_zetasql_subcatalogs_.emplace(space, std::move(new_catalog))
                .second);
      }
    }
    catalog->AddOwnedFunction(path.back(), std::move(function_pair.second));
  }
}

void SimpleCatalog::ClearFunctions() {
  absl::MutexLock l(&mutex_);
  functions_.clear();
  owned_functions_.clear();
  for (const auto& pair : owned_zetasql_subcatalogs_) {
    catalogs_.erase(pair.first);
  }
  owned_zetasql_subcatalogs_.clear();
}

void SimpleCatalog::ClearTableValuedFunctions() {
  absl::MutexLock l(&mutex_);
  table_valued_functions_.clear();
  owned_table_valued_functions_.clear();
  for (const auto& pair : owned_zetasql_subcatalogs_) {
    catalogs_.erase(pair.first);
  }
  owned_zetasql_subcatalogs_.clear();
}

TypeFactory* SimpleCatalog::type_factory() {
  absl::MutexLock l(&mutex_);
  if (type_factory_ == nullptr) {
    DCHECK(owned_type_factory_ == nullptr);
    owned_type_factory_ = absl::make_unique<TypeFactory>();
    type_factory_ = owned_type_factory_.get();
  }
  return type_factory_;
}

namespace {

absl::Status DeserializeImpl(
    const SimpleCatalogProto& proto,
    const std::vector<const google::protobuf::DescriptorPool*>& pools,
    SimpleCatalog* catalog) {
  for (const auto& table_proto : proto.table()) {
    std::unique_ptr<SimpleTable> table;
    ZETASQL_RETURN_IF_ERROR(SimpleTable::Deserialize(
        table_proto, pools, catalog->type_factory(), &table));
    const std::string& name = table_proto.has_name_in_catalog()
                                  ? table_proto.name_in_catalog()
                                  : table_proto.name();
    if (!catalog->AddOwnedTableIfNotPresent(name, std::move(table))) {
      return ::zetasql_base::InvalidArgumentErrorBuilder()
             << "Duplicate table '" << name << "' in serialized catalog";
    }
  }
  for (const auto& named_type_proto : proto.named_type()) {
    const Type* type;
    ZETASQL_RETURN_IF_ERROR(
        catalog->type_factory()->DeserializeFromProtoUsingExistingPools(
            named_type_proto.type(), pools, &type));
    if (!catalog->AddTypeIfNotPresent(named_type_proto.name(), type)) {
      return ::zetasql_base::InvalidArgumentErrorBuilder()
             << "Duplicate type '" << named_type_proto.name()
             << "' in serialized catalog";
    }
  }
  for (const auto& catalog_proto : proto.catalog()) {
    std::unique_ptr<SimpleCatalog> sub_catalog(
        new SimpleCatalog(catalog_proto.name(), catalog->type_factory()));
    ZETASQL_RETURN_IF_ERROR(DeserializeImpl(catalog_proto, pools, sub_catalog.get()));
    if (!catalog->AddOwnedCatalogIfNotPresent(catalog_proto.name(),
                                              std::move(sub_catalog))) {
      return ::zetasql_base::InvalidArgumentErrorBuilder()
             << "Duplicate catalog '" << catalog_proto.name()
             << "' in serialized catalog";
    }
  }
  for (const auto& function_proto : proto.custom_function()) {
    std::unique_ptr<Function> function;
    ZETASQL_RETURN_IF_ERROR(Function::Deserialize(
        function_proto, pools, catalog->type_factory(), &function));
    const std::string name = function->Name();
    if (!catalog->AddOwnedFunctionIfNotPresent(&function)) {
      return ::zetasql_base::InvalidArgumentErrorBuilder()
             << "Duplicate function '" << name << "' in serialized catalog";
    }
  }
  for (const auto& procedure_proto : proto.procedure()) {
    std::unique_ptr<Procedure> procedure;
    ZETASQL_RETURN_IF_ERROR(Procedure::Deserialize(
        procedure_proto, pools, catalog->type_factory(), &procedure));
    const std::string name = procedure->Name();
    if (!catalog->AddOwnedProcedureIfNotPresent(std::move(procedure))) {
      return ::zetasql_base::InvalidArgumentErrorBuilder()
             << "Duplicate procedure '" << name << "' in serialized catalog";
    }
  }
  if (proto.has_builtin_function_options()) {
    ZetaSQLBuiltinFunctionOptions options(proto.builtin_function_options());
    catalog->AddZetaSQLFunctions(options);
  }
  for (const auto& tvf_proto : proto.custom_tvf()) {
    std::unique_ptr<TableValuedFunction> tvf;
    ZETASQL_RETURN_IF_ERROR(TableValuedFunction::Deserialize(
        tvf_proto, pools, catalog->type_factory(), &tvf));
    const std::string name = tvf->Name();
    if (!catalog->AddOwnedTableValuedFunctionIfNotPresent(&tvf)) {
      return ::zetasql_base::InvalidArgumentErrorBuilder()
             << "Duplicate TVF '" << name << "' in serialized catalog";
    }
  }
  for (const auto& constant_proto : proto.constant()) {
    std::unique_ptr<SimpleConstant> constant;
    ZETASQL_RETURN_IF_ERROR(SimpleConstant::Deserialize(
        constant_proto, pools, catalog->type_factory(), &constant));
    const std::string name = constant->Name();
    if (!catalog->AddOwnedConstantIfNotPresent(std::move(constant))) {
      return ::zetasql_base::InvalidArgumentErrorBuilder()
             << "Duplicate constant '" << name << "' in serialized catalog";
    }
  }

  if (proto.has_file_descriptor_set_index()) {
    catalog->SetDescriptorPool(pools[proto.file_descriptor_set_index()]);
  }

  return absl::OkStatus();
}

template <typename M, typename ValueContainer>
void InsertValuesFromMap(const M& m, ValueContainer* value_container) {
  for (const auto& kv : m) {
    value_container->insert(kv.second);
  }
}

}  // namespace

absl::Status SimpleCatalog::Deserialize(
    const SimpleCatalogProto& proto,
    const std::vector<const google::protobuf::DescriptorPool*>& pools,
    std::unique_ptr<SimpleCatalog>* result) {
  // Create a top level catalog that owns the TypeFactory.
  std::unique_ptr<SimpleCatalog> catalog(new SimpleCatalog(proto.name()));
  ZETASQL_RETURN_IF_ERROR(DeserializeImpl(proto, pools, catalog.get()));
  *result = std::move(catalog);
  return absl::OkStatus();
}

absl::Status SimpleCatalog::Serialize(
    FileDescriptorSetMap* file_descriptor_set_map,
    SimpleCatalogProto* proto,
    bool ignore_builtin,
    bool ignore_recursive) const {
  absl::flat_hash_set<const Catalog*> seen;
  return SerializeImpl(
      &seen, file_descriptor_set_map, proto,
      ignore_builtin, ignore_recursive);
}

absl::Status SimpleCatalog::SerializeImpl(
    absl::flat_hash_set<const Catalog*>* seen_catalogs,
    FileDescriptorSetMap* file_descriptor_set_map, SimpleCatalogProto* proto,
    bool ignore_builtin, bool ignore_recursive) const {
  seen_catalogs->insert(this);

  absl::MutexLock l(&mutex_);

  proto->Clear();
  proto->set_name(name_);

  // Convert hash maps to std::maps so that the serialization output is
  // deterministic.
  const std::map<std::string, const Table*> tables(tables_.begin(),
                                                   tables_.end());
  const std::map<std::string, const Model*> models(models_.begin(),
                                                   models_.end());
  const std::map<std::string, const Type*> types(types_.begin(), types_.end());
  const std::map<std::string, const Function*> functions(functions_.begin(),
                                                         functions_.end());
  const std::map<std::string, const TableValuedFunction*>
      table_valued_functions(table_valued_functions_.begin(),
                             table_valued_functions_.end());
  const std::map<std::string, const Procedure*> procedures(procedures_.begin(),
                                                           procedures_.end());
  const std::map<std::string, const Catalog*> catalogs(catalogs_.begin(),
                                                       catalogs_.end());
  const std::map<std::string, const Constant*> constants(constants_.begin(),
                                                         constants_.end());

  for (const auto& entry : tables) {
    const std::string& table_name = entry.first;
    const Table* const table = entry.second;
    if (!table->Is<SimpleTable>()) {
      return ::zetasql_base::UnknownErrorBuilder()
             << "Cannot serialize non-SimpleTable " << table_name;
    }
    const SimpleTable* const simple_table = table->GetAs<SimpleTable>();
    SimpleTableProto* const table_proto = proto->add_table();
    ZETASQL_RETURN_IF_ERROR(
        simple_table->Serialize(file_descriptor_set_map, table_proto));
    if (absl::AsciiStrToLower(table_proto->name()) != table_name) {
      table_proto->set_name_in_catalog(table_name);
    }
  }

  for (const auto& entry : types) {
    const std::string& type_name = entry.first;
    const Type* const type = entry.second;
    SimpleCatalogProto::NamedTypeProto* named_type = proto->add_named_type();
    ZETASQL_RETURN_IF_ERROR(type->SerializeToProtoAndDistinctFileDescriptors(
        named_type->mutable_type(), file_descriptor_set_map));
    named_type->set_name(type_name);
  }

  for (const auto& entry : functions) {
    const Function* const function = entry.second;
    if (!(ignore_builtin && function->IsZetaSQLBuiltin())) {
      ZETASQL_RETURN_IF_ERROR(function->Serialize(file_descriptor_set_map,
                                          proto->add_custom_function()));
    }
  }

  for (const auto& entry : table_valued_functions) {
    const TableValuedFunction* const table_valued_function = entry.second;
    ZETASQL_RETURN_IF_ERROR(table_valued_function->Serialize(file_descriptor_set_map,
                                                     proto->add_custom_tvf()));
  }

  for (const auto& entry : procedures) {
    const Procedure* const procedure = entry.second;
    ZETASQL_RETURN_IF_ERROR(
        procedure->Serialize(file_descriptor_set_map, proto->add_procedure()));
  }

  for (const auto& entry : catalogs) {
    const std::string& catalog_name = entry.first;
    const Catalog* const catalog = entry.second;
    if (zetasql_base::ContainsKey(*seen_catalogs, catalog)) {
      if (ignore_recursive) {
        continue;
      } else {
        return ::zetasql_base::UnknownErrorBuilder()
               << "Recursive catalog not serializable.";
      }
    }

    if (ignore_builtin) {
      if (zetasql_base::ContainsKey(owned_zetasql_subcatalogs_, catalog_name)) {
        continue;
      }
    }

    if (!catalog->Is<SimpleCatalog>()) {
      return ::zetasql_base::UnknownErrorBuilder()
             << "Cannot serialize non-SimpleCatalog " << catalog_name;
    }
    const SimpleCatalog* const simple_catalog = catalog->GetAs<SimpleCatalog>();
    ZETASQL_RETURN_IF_ERROR(simple_catalog->Serialize(file_descriptor_set_map,
                                              proto->add_catalog()));
  }

  for (const auto& entry : constants) {
    const std::string& constant_name = entry.first;
    const Constant* const constant = entry.second;
    if (!constant->Is<SimpleConstant>()) {
      return ::zetasql_base::UnknownErrorBuilder()
             << "Cannot serialize non-SimpleConstant " << constant_name;
    }
    const SimpleConstant* const simple_constant =
        constant->GetAs<SimpleConstant>();
    ZETASQL_RETURN_IF_ERROR(simple_constant->Serialize(file_descriptor_set_map,
                                               proto->add_constant()));
  }

  return absl::OkStatus();
}

absl::Status SimpleCatalog::GetCatalogs(
    absl::flat_hash_set<const Catalog*>* output) const {
  ZETASQL_RET_CHECK_NE(output, nullptr);
  ZETASQL_RET_CHECK(output->empty());
  absl::MutexLock lock(&mutex_);
  InsertValuesFromMap(catalogs_, output);
  return absl::OkStatus();
}

absl::Status SimpleCatalog::GetTables(
    absl::flat_hash_set<const Table*>* output) const {
  ZETASQL_RET_CHECK_NE(output, nullptr);
  ZETASQL_RET_CHECK(output->empty());
  absl::MutexLock lock(&mutex_);
  InsertValuesFromMap(tables_, output);
  return absl::OkStatus();
}

absl::Status SimpleCatalog::GetTypes(
    absl::flat_hash_set<const Type*>* output) const {
  ZETASQL_RET_CHECK_NE(output, nullptr);
  ZETASQL_RET_CHECK(output->empty());
  absl::MutexLock lock(&mutex_);
  InsertValuesFromMap(types_, output);
  return absl::OkStatus();
}

absl::Status SimpleCatalog::GetFunctions(
    absl::flat_hash_set<const Function*>* output) const {
  ZETASQL_RET_CHECK_NE(output, nullptr);
  ZETASQL_RET_CHECK(output->empty());
  absl::MutexLock lock(&mutex_);
  InsertValuesFromMap(functions_, output);
  return absl::OkStatus();
}

std::vector<std::string> SimpleCatalog::table_names() const {
  absl::MutexLock l(&mutex_);
  std::vector<std::string> table_names;
  zetasql_base::AppendKeysFromMap(tables_, &table_names);
  return table_names;
}

std::vector<const Table*> SimpleCatalog::tables() const {
  absl::MutexLock l(&mutex_);
  std::vector<const Table*> tables;
  zetasql_base::AppendValuesFromMap(tables_, &tables);
  return tables;
}

std::vector<const Type*> SimpleCatalog::types() const {
  absl::MutexLock l(&mutex_);
  std::vector<const Type*> types;
  zetasql_base::AppendValuesFromMap(types_, &types);
  return types;
}

std::vector<std::string> SimpleCatalog::function_names() const {
  absl::MutexLock l(&mutex_);
  std::vector<std::string> function_names;
  zetasql_base::AppendKeysFromMap(functions_, &function_names);
  return function_names;
}

std::vector<const Function*> SimpleCatalog::functions() const {
  absl::MutexLock l(&mutex_);
  std::vector<const Function*> functions;
  zetasql_base::AppendValuesFromMap(functions_, &functions);
  return functions;
}

std::vector<std::string> SimpleCatalog::table_valued_function_names() const {
  absl::MutexLock l(&mutex_);
  std::vector<std::string> table_valued_function_names;
  zetasql_base::AppendKeysFromMap(table_valued_functions_, &table_valued_function_names);
  return table_valued_function_names;
}

std::vector<const TableValuedFunction*> SimpleCatalog::table_valued_functions()
    const {
  absl::MutexLock l(&mutex_);
  std::vector<const TableValuedFunction*> table_valued_functions;
  zetasql_base::AppendValuesFromMap(table_valued_functions_, &table_valued_functions);
  return table_valued_functions;
}

std::vector<const Procedure*> SimpleCatalog::procedures() const {
  absl::MutexLock l(&mutex_);
  std::vector<const Procedure*> procedures;
  zetasql_base::AppendValuesFromMap(procedures_, &procedures);
  return procedures;
}

std::vector<std::string> SimpleCatalog::catalog_names() const {
  absl::MutexLock l(&mutex_);
  std::vector<std::string> catalog_names;
  zetasql_base::AppendKeysFromMap(catalogs_, &catalog_names);
  return catalog_names;
}

std::vector<Catalog*> SimpleCatalog::catalogs() const {
  absl::MutexLock l(&mutex_);
  std::vector<Catalog*> catalogs;
  zetasql_base::AppendValuesFromMap(catalogs_, &catalogs);
  return catalogs;
}

std::vector<std::string> SimpleCatalog::constant_names() const {
  absl::MutexLock l(&mutex_);
  std::vector<std::string> constant_names;
  zetasql_base::AppendKeysFromMap(constants_, &constant_names);
  return constant_names;
}

std::vector<const Constant*> SimpleCatalog::constants() const {
  absl::MutexLock l(&mutex_);
  std::vector<const Constant*> constants;
  zetasql_base::AppendValuesFromMap(constants_, &constants);
  return constants;
}

SimpleTable::SimpleTable(const std::string& name,
                         const std::vector<NameAndType>& columns,
                         const int64_t serialization_id)
    : name_(name), id_(serialization_id) {
  for (const NameAndType& name_and_type : columns) {
    std::unique_ptr<SimpleColumn> column(
        new SimpleColumn(name, name_and_type.first, name_and_type.second));
    ZETASQL_CHECK_OK(AddColumn(column.release(), true /* is_owned */));
  }
}

SimpleTable::SimpleTable(const std::string& name,
                         const std::vector<const Column*>& columns,
                         bool take_ownership, const int64_t serialization_id)
    : name_(name), id_(serialization_id) {
  for (const Column* column : columns) {
    ZETASQL_CHECK_OK(AddColumn(column, take_ownership));
  }
}

// TODO: Consider changing the implicit name of the
// value table column to match the table name, rather than hardcoding
// this to "value".  Generally this should not be user-facing, but there
// are some cases where this appears in error messages and a reference
// to something named 'value' is confusing there.
SimpleTable::SimpleTable(const std::string& name, const Type* row_type,
                         const int64_t id)
    : SimpleTable(name, {{"value", row_type}}, id) {
  is_value_table_ = true;
}

SimpleTable::SimpleTable(const std::string& name, const int64_t id)
    : name_(name), id_(id) {}

const Column* SimpleTable::FindColumnByName(const std::string& name) const {
  if (name.empty()) {
    return nullptr;
  }
  return zetasql_base::FindPtrOrNull(columns_map_, absl::AsciiStrToLower(name));
}

absl::Status SimpleTable::AddColumn(const Column* column, bool is_owned) {
  std::unique_ptr<const Column> column_owner;
  if (is_owned) {
    column_owner.reset(column);
  }
  ZETASQL_RETURN_IF_ERROR(InsertColumnToColumnMap(column));
  columns_.push_back(column);
  if (is_owned) {
    owned_columns_.emplace_back(std::move(column_owner));
  }
  return absl::OkStatus();
}

absl::Status SimpleTable::SetPrimaryKey(std::vector<int> primary_key) {
  for (int column_index : primary_key) {
    if (column_index >= NumColumns()) {
      return ::zetasql_base::InvalidArgumentErrorBuilder()
             << "Invalid column index " << column_index << "in primary key";
    }
  }
  primary_key_.emplace(primary_key);
  return absl::OkStatus();
}

absl::Status SimpleTable::InsertColumnToColumnMap(const Column* column) {
  const std::string column_name = absl::AsciiStrToLower(column->Name());
  if (!allow_anonymous_column_name_ && column_name.empty()) {
    return ::zetasql_base::InvalidArgumentErrorBuilder()
           << "Empty column names not allowed";
  }

  if (zetasql_base::ContainsKey(columns_map_, column_name)) {
    if (!allow_duplicate_column_names_) {
      return ::zetasql_base::InvalidArgumentErrorBuilder()
             << "Duplicate column in " << FullName() << ": " << column->Name();
    }
    columns_map_.erase(column_name);
    ZETASQL_RET_CHECK(zetasql_base::InsertIfNotPresent(&duplicate_column_names_, column_name))
        << column_name;
  } else if (!zetasql_base::ContainsKey(duplicate_column_names_, column_name)) {
    ZETASQL_RET_CHECK(zetasql_base::InsertIfNotPresent(&columns_map_, column_name, column))
        << column_name;
  }

  if (column_name.empty()) {
    anonymous_column_seen_ = true;
  }
  return absl::OkStatus();
}

void SimpleTable::SetContents(const std::vector<std::vector<Value>>& rows) {
  column_major_contents_.clear();
  column_major_contents_.resize(NumColumns());
  for (int i = 0; i < NumColumns(); ++i) {
    auto column_values = std::make_shared<std::vector<Value>>();
    column_values->reserve(rows.size());
    for (int j = 0; j < rows.size(); ++j) {
      column_values->push_back(rows[j][i]);
    }
    column_major_contents_[i] = column_values;
  }

  auto factory = [this, rows](absl::Span<const int> column_idxs)
      -> zetasql_base::StatusOr<std::unique_ptr<EvaluatorTableIterator>> {
    std::vector<const Column*> columns;
    std::vector<std::shared_ptr<const std::vector<Value>>> column_values;
    column_values.reserve(column_idxs.size());
    for (const int column_idx : column_idxs) {
      columns.push_back(GetColumn(column_idx));
      column_values.push_back(column_major_contents_[column_idx]);
    }
    std::unique_ptr<EvaluatorTableIterator> iter(
        new SimpleEvaluatorTableIterator(
            columns, column_values,
            /*end_status=*/absl::OkStatus(), /*filter_column_idxs=*/{},
            /*cancel_cb=*/[]() {},
            /*set_deadline_cb=*/[](absl::Time t) {}, zetasql_base::Clock::RealClock()));
    return iter;
  };

  SetEvaluatorTableIteratorFactory(factory);
}

zetasql_base::StatusOr<std::unique_ptr<EvaluatorTableIterator>>
SimpleTable::CreateEvaluatorTableIterator(
    absl::Span<const int> column_idxs) const {
  if (evaluator_table_iterator_factory_ == nullptr) {
    // Returns an error.
    return Table::CreateEvaluatorTableIterator(column_idxs);
  }
  return (*evaluator_table_iterator_factory_)(column_idxs);
}

absl::Status SimpleTable::Serialize(
    FileDescriptorSetMap* file_descriptor_set_map,
    SimpleTableProto* proto) const {
  proto->Clear();
  proto->set_name(Name());
  if (GetSerializationId() > 0) {
    proto->set_serialization_id(GetSerializationId());
  }
  proto->set_is_value_table(IsValueTable());
  for (const Column* column : columns_) {
    auto* column_proto = proto->add_column();
    ZETASQL_RETURN_IF_ERROR(static_cast<const SimpleColumn*>(column)->Serialize(
        file_descriptor_set_map, column_proto));
  }
  if (primary_key_.has_value()) {
    for (int column_index : primary_key_.value()) {
      proto->add_primary_key_column_index(column_index);
    }
  }
  if (allow_anonymous_column_name_) {
    proto->set_allow_anonymous_column_name(true);
  }
  if (allow_duplicate_column_names_) {
    proto->set_allow_duplicate_column_names(true);
  }
  return absl::OkStatus();
}

absl::Status SimpleTable::Deserialize(
      const SimpleTableProto& proto,
      const std::vector<const google::protobuf::DescriptorPool*>& pools,
      TypeFactory* factory,
      std::unique_ptr<SimpleTable>* result) {
  std::unique_ptr<SimpleTable> table(
      new SimpleTable(proto.name(), proto.serialization_id()));
  table->set_is_value_table(proto.is_value_table());
  ZETASQL_RETURN_IF_ERROR(table->set_allow_anonymous_column_name(
      proto.allow_anonymous_column_name()));
  ZETASQL_RETURN_IF_ERROR(table->set_allow_duplicate_column_names(
      proto.allow_duplicate_column_names()));

  for (const SimpleColumnProto& column_proto : proto.column()) {
    std::unique_ptr<SimpleColumn> column;
    ZETASQL_RETURN_IF_ERROR(SimpleColumn::Deserialize(
        column_proto, table->Name(), pools, factory, &column));
    ZETASQL_RETURN_IF_ERROR(table->AddColumn(column.release(), true  /* owned */));
  }

  if (proto.primary_key_column_index_size() > 0) {
    std::vector<int> primary_key;
    for (int column_index : proto.primary_key_column_index()) {
      primary_key.push_back(column_index);
    }
    ZETASQL_RETURN_IF_ERROR(table->SetPrimaryKey(primary_key));
  }

  *result = std::move(table);
  return absl::OkStatus();
}

SimpleColumn::SimpleColumn(const std::string& table_name,
                           const std::string& name, const Type* type,
                           bool is_pseudo_column, bool is_writable_column)
    : name_(name),
      full_name_(absl::StrCat(table_name, ".", name)),
      type_(type),
      is_pseudo_column_(is_pseudo_column),
      is_writable_column_(is_writable_column) {}

SimpleColumn::~SimpleColumn() {
}

absl::Status SimpleColumn::Serialize(
    FileDescriptorSetMap* file_descriptor_set_map,
    SimpleColumnProto* proto) const {
  proto->Clear();
  proto->set_name(Name());
  ZETASQL_RETURN_IF_ERROR(GetType()->SerializeToProtoAndDistinctFileDescriptors(
      proto->mutable_type(), file_descriptor_set_map));
  proto->set_is_pseudo_column(IsPseudoColumn());
  if (!IsWritableColumn()) {
    proto->set_is_writable_column(false);
  }
  return absl::OkStatus();
}

absl::Status SimpleColumn::Deserialize(
    const SimpleColumnProto& proto, const std::string& table_name,
    const std::vector<const google::protobuf::DescriptorPool*>& pools,
    TypeFactory* factory, std::unique_ptr<SimpleColumn>* result) {
  const Type* type;
  ZETASQL_RETURN_IF_ERROR(factory->DeserializeFromProtoUsingExistingPools(
      proto.type(), pools, &type));
  auto column = absl::make_unique<SimpleColumn>(table_name, proto.name(), type,
                                                proto.is_pseudo_column(),
                                                proto.is_writable_column());
  *result = std::move(column);
  return absl::OkStatus();
}

// static
absl::Status SimpleConstant::Create(
    const std::vector<std::string>& name_path, const Value& value,
    std::unique_ptr<SimpleConstant>* simple_constant) {
  ZETASQL_RET_CHECK(!name_path.empty());
  ZETASQL_RET_CHECK(value.is_valid());
  simple_constant->reset(new SimpleConstant(name_path, value));
  return absl::OkStatus();
}

std::string SimpleConstant::DebugString() const {
  return absl::StrCat(FullName(), "=", value().DebugString());
}

std::string SimpleConstant::VerboseDebugString() const {
  return absl::StrCat(DebugString(), " (", type()->DebugString(), ")");
}

absl::Status SimpleConstant::Serialize(
    FileDescriptorSetMap* file_descriptor_set_map,
    SimpleConstantProto* simple_constant_proto) const {
  for (const std::string& name : name_path()) {
    simple_constant_proto->add_name_path(name);
  }
  ZETASQL_RETURN_IF_ERROR(value().type()->SerializeToProtoAndDistinctFileDescriptors(
      simple_constant_proto->mutable_type(), file_descriptor_set_map));
  ZETASQL_RETURN_IF_ERROR(value().Serialize(simple_constant_proto->mutable_value()));
  return absl::OkStatus();
}

absl::Status SimpleConstant::Deserialize(
    const SimpleConstantProto& simple_constant_proto,
    const std::vector<const google::protobuf::DescriptorPool*>& descriptor_pools,
    TypeFactory* type_factory,
    std::unique_ptr<SimpleConstant>* simple_constant) {
  std::vector<std::string> name_path;
  for (const std::string& name : simple_constant_proto.name_path()) {
    name_path.push_back(name);
  }
  const Type* type;
  ZETASQL_RETURN_IF_ERROR(type_factory->DeserializeFromProtoUsingExistingPools(
      simple_constant_proto.type(), descriptor_pools, &type));
  ZETASQL_ASSIGN_OR_RETURN(const Value value,
                   Value::Deserialize(simple_constant_proto.value(), type));
  simple_constant->reset(new SimpleConstant(name_path, value));
  return absl::OkStatus();
}

SimpleModel::SimpleModel(const std::string& name,
                         const std::vector<NameAndType>& inputs,
                         const std::vector<NameAndType>& outputs,
                         const int64_t id)
    : name_(name), id_(id) {
  for (const NameAndType& name_and_type : inputs) {
    std::unique_ptr<SimpleColumn> column(
        new SimpleColumn(name, name_and_type.first, name_and_type.second));
    ZETASQL_CHECK_OK(AddInput(column.release(), true /* is_owned */));
  }
  for (const NameAndType& name_and_type : outputs) {
    std::unique_ptr<SimpleColumn> column(
        new SimpleColumn(name, name_and_type.first, name_and_type.second));
    ZETASQL_CHECK_OK(AddOutput(column.release(), true /* is_owned */));
  }
}

SimpleModel::SimpleModel(const std::string& name,
                         const std::vector<const Column*>& inputs,
                         const std::vector<const Column*>& outputs,
                         bool take_ownership, const int64_t id)
    : name_(name), id_(id) {
  for (const Column* column : inputs) {
    ZETASQL_CHECK_OK(AddInput(column, take_ownership));
  }
  for (const Column* column : outputs) {
    ZETASQL_CHECK_OK(AddOutput(column, take_ownership));
  }
}

const Column* SimpleModel::FindInputByName(const std::string& name) const {
  if (name.empty()) {
    return nullptr;
  }
  return zetasql_base::FindPtrOrNull(inputs_map_, absl::AsciiStrToLower(name));
}

const Column* SimpleModel::FindOutputByName(const std::string& name) const {
  if (name.empty()) {
    return nullptr;
  }
  return zetasql_base::FindPtrOrNull(outputs_map_, absl::AsciiStrToLower(name));
}
absl::Status SimpleModel::AddInput(const Column* column, bool is_owned) {
  std::unique_ptr<const Column> column_owner;
  if (is_owned) {
    column_owner.reset(column);
  }
  const std::string column_name = absl::AsciiStrToLower(column->Name());
  if (!zetasql_base::InsertIfNotPresent(&inputs_map_, column_name, column)) {
    return ::zetasql_base::InvalidArgumentErrorBuilder()
           << "Duplicate input column in " << FullName() << ": "
           << column->Name();
  }
  inputs_.push_back(column);
  if (is_owned) {
    owned_inputs_outputs_.emplace_back(std::move(column_owner));
  }
  return absl::OkStatus();
}

absl::Status SimpleModel::AddOutput(const Column* column, bool is_owned) {
  std::unique_ptr<const Column> column_owner;
  if (is_owned) {
    column_owner.reset(column);
  }
  const std::string column_name = absl::AsciiStrToLower(column->Name());
  if (!zetasql_base::InsertIfNotPresent(&outputs_map_, column_name, column)) {
    return ::zetasql_base::InvalidArgumentErrorBuilder()
           << "Duplicate output column in " << FullName() << ": "
           << column->Name();
  }
  outputs_.push_back(column);
  if (is_owned) {
    owned_inputs_outputs_.emplace_back(std::move(column_owner));
  }
  return absl::OkStatus();
}

}  // namespace zetasql
