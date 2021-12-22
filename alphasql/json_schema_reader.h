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
#include "alphasql/proto/alphasql_service.pb.h"
#include "zetasql/base/status.h"
#include "zetasql/base/status_macros.h"
#include "zetasql/base/statusor.h"
#include "zetasql/public/simple_catalog.h"
#include "zetasql/public/types/type_factory.h"
#include <boost/foreach.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <google/protobuf/util/json_util.h>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>

namespace alphasql {

std::map<SupportedType, zetasql::TypeKind> FromBigQueryTypeToZetaSQLTypeMap = {
    {STRING, zetasql::TYPE_STRING},     {INT64, zetasql::TYPE_INT64},
    {INTEGER, zetasql::TYPE_INT64},     {BOOL, zetasql::TYPE_BOOL},
    {BOOLEAN, zetasql::TYPE_BOOL},      {FLOAT64, zetasql::TYPE_FLOAT},
    {FLOAT, zetasql::TYPE_FLOAT},       {NUMERIC, zetasql::TYPE_NUMERIC},
    {BYTES, zetasql::TYPE_BYTES},       {TIMESTAMP, zetasql::TYPE_TIMESTAMP},
    {DATE, zetasql::TYPE_DATE},         {TIME, zetasql::TYPE_TIME},
    {DATETIME, zetasql::TYPE_DATETIME}, {GEOGRAPHY, zetasql::TYPE_GEOGRAPHY},
};

static zetasql::TypeFactory tf;

// TODO: Handle return statuses of type:: functions
absl::Status ConvertSupportedTypeToZetaSQLType(const zetasql::Type **zetasql_type,
                                       const Column *column) {
  if (column->mode() == REPEATED && column->type() != RECORD) {
    // Array types
    *zetasql_type = zetasql::types::ArrayTypeFromSimpleTypeKind(
        FromBigQueryTypeToZetaSQLTypeMap[column->type()]);
    return absl::OkStatus();
  }
  if (column->type() != RECORD) {
    *zetasql_type = zetasql::types::TypeFromSimpleTypeKind(
        FromBigQueryTypeToZetaSQLTypeMap[column->type()]);
    return absl::OkStatus();
  }
  // Struct types
  std::vector<zetasql::StructField> fields;
  for (const auto &field : column->fields()) {
    const zetasql::Type *field_type;
    const auto status = ConvertSupportedTypeToZetaSQLType(&field_type, &field);
    if (!status.ok()) {
      return status;
    }
    fields.push_back(zetasql::StructField(field.name(), field_type));
  }
  if (column->mode() != REPEATED) {
    const auto status = tf.MakeStructTypeFromVector(fields, zetasql_type);
    if (!status.ok()) {
      std::cerr << "ERROR converting record " << column->name() << " failed." << std::endl;
      return status;
    }
    return absl::OkStatus();
  }
  const zetasql::Type *element_type;
  auto status = tf.MakeStructTypeFromVector(fields, &element_type);
  if (!status.ok()) {
    std::cerr << "ERROR converting repeated record " << column->name() << " failed." << std::endl;
    return status;
  }

  status = tf.MakeArrayType(element_type, zetasql_type);
  if (!status.ok()) {
    std::cerr << "ERROR converting repeated record " << column->name() << " failed." << std::endl;
    return status;
  }

  return absl::OkStatus;
}

absl::Status AddColumnToTable(zetasql::SimpleTable *table, const std::string field) {
  Column column_msg;
  google::protobuf::util::JsonParseOptions jsonParseOptions;
  jsonParseOptions.ignore_unknown_fields = true;
  auto status = google::protobuf::util::JsonStringToMessage(
      field, &column_msg, jsonParseOptions);
  if (!status.ok()) {
    return status;
  }

  const zetasql::Type *zetasql_type;

  status = ConvertSupportedTypeToZetaSQLType(&zetasql_type, &column_msg);
  if (!status.ok()) {
    return status;
  }

  if (zetasql_type == nullptr) {
    std::string message;
    google::protobuf::util::MessageToJsonString(column_msg, &message);
    return absl::InvalidArgumentError("ERROR: invalid column " + message);
  }

  std::unique_ptr<zetasql::SimpleColumn> zetasql_column(
      new zetasql::SimpleColumn(table->Name(), column_msg.name(),
                                zetasql_type));
  table->AddColumn(zetasql_column.release(), true);
  return absl::OkStatus();
}

void UpdateCatalogFromJSON(const std::string &json_schema_path,
                           zetasql::SimpleCatalog *catalog) {
  if (!std::filesystem::is_regular_file(json_schema_path) &&
      !std::filesystem::is_fifo(json_schema_path)) {
    std::cerr << "ERROR: not a json file path [at " << json_schema_path << ":1:1]"
              << std::endl;
    throw;
  }

  using namespace boost;
  property_tree::ptree pt;
  property_tree::read_json(json_schema_path, pt);

  std::vector<std::unique_ptr<zetasql::SimpleTable>> tables;
  for (property_tree::ptree::const_iterator it = pt.begin(); it != pt.end();
       ++it) {
    const auto table_name = it->first;
    const property_tree::ptree &schema = it->second;
    std::unique_ptr<zetasql::SimpleTable> table(
        new zetasql::SimpleTable(table_name));
    for (property_tree::ptree::const_iterator it = schema.begin();
         it != schema.end(); ++it) {
      std::ostringstream oss;
      property_tree::write_json(oss, it->second);
      auto status = AddColumnToTable(table.get(), oss.str());
      if (!status.ok()) {
        status = zetasql::UpdateErrorLocationPayloadWithFilenameIfNotPresent(status, json_schema_path);
        std::cerr << "Failed to generate catalog from JSON file: " << status << std::endl;
        throw;
      }
    }
    catalog->AddTable(table.release());
  }

  return;
}

} // namespace alphasql
