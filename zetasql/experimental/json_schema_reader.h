#include <iostream>
#include <string>
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

void UpdateCatalogFromJSON(const std::string& json_schema_path, SimpleCatalog* catalog) {
  if (!std::filesystem::is_regular_file(json_schema_path)) {
    std::cout << "ERROR: not a json file path " << json_schema_path << std::endl;
    return;
  }

  using namespace boost;
  property_tree::ptree pt;
  property_tree::read_json(json_schema_path, pt);
  BOOST_FOREACH(property_tree::ptree::value_type &schema, pt.get_child("table_schemas")) {
    std::string table_name = schema.second.get<std::string>("name");
    std::unique_ptr<SimpleTable> table(new SimpleTable(table_name));
    BOOST_FOREACH(property_tree::ptree::value_type &field, schema.second.get_child("schema")) {
      std::string mode = field.second.get<std::string>("mode");
      std::string type_string = field.second.get<std::string>("type");
      mode = absl::AsciiStrToUpper(mode);
      type_string = absl::AsciiStrToUpper(type_string);

      // TODO(Matts966): Implement Array and Struct types
      if (mode == "REPEATED") {
        if (type_string == "RECORD") {

        } else {

        }
        std::cout << "ERROR: unsupported type " + type_string + "\n" << std::endl;
        throw;
      } else {

      }

      if (FromBigQueryTypeToZetaSQLTypeMap.count(type_string) == 0) {
        std::cout << "ERROR: unsupported type " + type_string + "\n" << std::endl;
        throw;
      }

      const auto zetasql_type = types::TypeFromSimpleTypeKind(FromBigQueryTypeToZetaSQLTypeMap[type_string]);
      if (zetasql_type == nullptr) {
        std::cout << "ERROR: unsupported type " + type_string + "\n" << std::endl;
        throw;
      }

      std::unique_ptr<SimpleColumn> column(
        new SimpleColumn(table_name, field.second.get<std::string>("name"), zetasql_type));
      table->AddColumn(column.release(), true);
    }
    catalog->AddTable(table->Name(), table.release());
  }

  return;
}

}
