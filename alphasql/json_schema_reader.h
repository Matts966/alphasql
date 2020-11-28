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

#include <iostream>
#include <string>
#include <tuple>
#include <boost/property_tree/json_parser.hpp>
#include <boost/foreach.hpp>
#include "zetasql/public/simple_catalog.h"
#include "zetasql/public/types/type_factory.h"
#include "zetasql/base/status.h"
#include "zetasql/base/status_macros.h"
#include "zetasql/base/statusor.h"
#include "absl/strings/ascii.h"

namespace zetasql {

std::map<std::string, TypeKind> FromBigQueryTypeToZetaSQLTypeMap = {
  {"STRING", TYPE_STRING},
  {"INT64", TYPE_INT64},
  {"INTEGER", TYPE_INT64},
  {"BOOL", TYPE_BOOL},
  {"BOOLEAN", TYPE_BOOL},
  {"FLOAT64", TYPE_FLOAT},
  {"FLOAT", TYPE_FLOAT},
  {"NUMERIC", TYPE_NUMERIC},
  {"BYTES", TYPE_BYTES},
  {"TIMESTAMP", TYPE_TIMESTAMP},
  {"DATE", TYPE_DATE},
  {"TIME", TYPE_TIME},
  {"DATETIME", TYPE_DATETIME},
  {"GEOGRAPHY", TYPE_GEOGRAPHY},
};

void AddColumnToTable(SimpleTable* table, const boost::property_tree::ptree::value_type field) {
  std::string mode = field.second.get<std::string>("mode");
  std::string type_string = field.second.get<std::string>("type");
  mode = absl::AsciiStrToUpper(mode);
  type_string = absl::AsciiStrToUpper(type_string);

  if (FromBigQueryTypeToZetaSQLTypeMap.count(type_string) == 0) {
    std::cout << "ERROR: unsupported type " + type_string + "\n" << std::endl;
    throw;
  }

  const zetasql::Type* zetasql_type;

  // TODO(Matts966): Implement Struct types
  if (mode == "REPEATED" && type_string != "RECORD") {
    // Array types
    zetasql_type = types::ArrayTypeFromSimpleTypeKind(FromBigQueryTypeToZetaSQLTypeMap[type_string]);
  } else {
    zetasql_type = types::TypeFromSimpleTypeKind(FromBigQueryTypeToZetaSQLTypeMap[type_string]);
  }

  if (zetasql_type == nullptr) {
    std::cout << "ERROR: unsupported type " + type_string + "\n" << std::endl;
    throw;
  }

  std::unique_ptr<SimpleColumn> column(
      new SimpleColumn(table->Name(), field.second.get<std::string>("name"), zetasql_type));
  table->AddColumn(column.release(), true);
}

void UpdateCatalogFromJSON(const std::string& json_schema_path, SimpleCatalog* catalog) {
  if (!std::filesystem::is_regular_file(json_schema_path) & !std::filesystem::is_fifo(json_schema_path)) {
    std::cout << "ERROR: not a json file path " << json_schema_path << std::endl;
    return;
  }

  using namespace boost;
  property_tree::ptree pt;
  property_tree::read_json(json_schema_path, pt);

  std::string table_name;
  for (property_tree::ptree::const_iterator it = pt.begin(); it != pt.end(); ++it) {
    table_name = it->first;
    const property_tree::ptree& schema = it->second;
    std::unique_ptr<SimpleTable> table(new SimpleTable(table_name));
    BOOST_FOREACH(const property_tree::ptree::value_type &field, schema) {
      AddColumnToTable(table.get(), field);
    }

    catalog->AddTable(table.release());
  }

  return;
}

}
