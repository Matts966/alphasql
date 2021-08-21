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

#include "absl/strings/ascii.h"
#include "zetasql/base/status.h"
#include "zetasql/base/status_macros.h"
#include "zetasql/base/statusor.h"
#include "zetasql/public/simple_catalog.h"
#include "zetasql/public/types/type_factory.h"
#include "alphasql/proto/alphasql_service.pb.h"
#include <boost/foreach.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <iostream>
#include <string>
#include <tuple>
#include <google/protobuf/util/json_util.h>

namespace zetasql {

std::map<SupportedType, TypeKind> FromBigQueryTypeToZetaSQLTypeMap = {
    {SupportedType.STRING, TYPE_STRING},     {SupportedType.INT64, TYPE_INT64},
    {SupportedType.INTEGER, TYPE_INT64},     {SupportedType.BOOL, TYPE_BOOL},
    {SupportedType.BOOLEAN, TYPE_BOOL},      {SupportedType.FLOAT64, TYPE_FLOAT},
    {SupportedType.FLOAT, TYPE_FLOAT},       {SupportedType.NUMERIC, TYPE_NUMERIC},
    {SupportedType.BYTES, TYPE_BYTES},       {SupportedType.TIMESTAMP, TYPE_TIMESTAMP},
    {SupportedType.DATE, TYPE_DATE},         {SupportedType.TIME, TYPE_TIME},
    {SupportedType.DATETIME, TYPE_DATETIME}, {SupportedType.GEOGRAPHY, TYPE_GEOGRAPHY},
};

// TODO: Handle return statuses of type:: functions
void ConvertSupportedTypeToZetaSQLType(zetasql::Type **zetasql_type, SupportedType *type) {
  if (type.mode() == Mode.REPEATED && type.type() != Mode.RECORD) {
    // Array types
    *zetasql_type = types::ArrayTypeFromSimpleTypeKind(
        FromBigQueryTypeToZetaSQLTypeMap[type.type()]);
    return;
  }
  if (type.type() != Mode.RECORD) {
    *zetasql_type = types::TypeFromSimpleTypeKind(
        FromBigQueryTypeToZetaSQLTypeMap[type.type()]);
    return;
  }
  // Struct types
  const vector<zetasql::StructField> fields;
  for (const auto& field : type.fields()) {
    fields.push_back(zetasql::StructField(field.name(), FromBigQueryTypeToZetaSQLTypeMap[field.type()]));
  }
  if (type.type() != Mode.REPEATED) {
    types::MakeStructTypeFromVector(fields, zetasql_type);
    return;
  }
  const zetasql::Type *element_type;
  types::MakeStructTypeFromVector(fields, &element_type);
  types::MakeArrayType(element_type, zetasql_type);
}

void AddColumnToTable(SimpleTable *table,
                      const boost::property_tree::ptree::value_type field) {
  using namespace google::protobuf::util;
  Field fieldMsg;
  const auto status = util::JsonStringToMessage(
        field.first, &fieldMsg, util::JsonParseOptions(true, true));
  if (!status.ok()) {
    std::cout << "ERROR: " << status << std::endl;
    throw;
  }
  /* std::string mode = field.second.get<std::string>("mode"); */
  /* std::string type_string = field.second.get<std::string>("type"); */
  /* mode = absl::AsciiStrToUpper(mode); */
  /* type_string = absl::AsciiStrToUpper(type_string); */

  /* if (FromBigQueryTypeToZetaSQLTypeMap.count(type_string) == 0) { */
  /*   std::cout << "ERROR: unsupported type " + type_string + "\n" << std::endl; */
  /*   throw; */
  /* } */

  const zetasql::Type *zetasql_type;

  if (fieldMsg.has_type()) {
    ConvertSupportedTypeToZetaSQLType(&zetasql_type, &fieldMsg.type());
  } else {
    zetasql_type = &fieldMsg.zetasql_type();
  }

  if (zetasql_type == nullptr) {
    std::cout << "ERROR: invalid field " << fieldMsg << std::endl;
    throw;
  }

  std::unique_ptr<SimpleColumn> column(new SimpleColumn(
      table->Name(), field.second.get<std::string>("name"), zetasql_type));
  table->AddColumn(column.release(), true);
}

void UpdateCatalogFromJSON(const std::string &json_schema_path,
                           SimpleCatalog *catalog) {
  if (!std::filesystem::is_regular_file(json_schema_path) &
      !std::filesystem::is_fifo(json_schema_path)) {
    std::cout << "ERROR: not a json file path " << json_schema_path
              << std::endl;
    return;
  }

  using namespace boost;
  property_tree::ptree pt;
  property_tree::read_json(json_schema_path, pt);

  std::string table_name;
  for (property_tree::ptree::const_iterator it = pt.begin(); it != pt.end();
       ++it) {
    table_name = it->first;
    const property_tree::ptree &schema = it->second;
    std::unique_ptr<SimpleTable> table(new SimpleTable(table_name));
    BOOST_FOREACH (const property_tree::ptree::value_type &field, schema) {
      AddColumnToTable(table.get(), field);
    }

    catalog->AddTable(table.release());
  }

  return;
}

} // namespace zetasql
