#include <iostream>
#include <string>
#include <boost/property_tree/json_parser.hpp>
#include <boost/foreach.hpp>
#include "zetasql/public/simple_catalog.h"
#include "zetasql/public/type.h"

namespace zetasql {

// template <typename T>
// std::vector<T> as_vector(boost::property_tree::ptree const& pt,
//                          boost::property_tree::ptree::key_type const& key) {
//     std::vector<T> r;
//     for (auto& item : pt.get_child(key))
//         r.push_back(item.second.get_value<T>());
//     return r;
// }

// const Type* FromBigQueryTypeToZetaSQLType(std::string type) {
//   switch (type) {
//     case "STRING": return types::StringType();
//     case "INTEGER": return types::Int64Type();
//     case "INT64": return types::Int64Type();
//     case "BOOL": return types::BoolType();
//     case "BOOLEAN": return types::BoolType();
//     case "FLOAT64": return types::FloatType();
//     case "NUMERIC": return types::NumericType();
//     case "BYTES": return types::BytesType();
//     case "TIMESSTAMP": return types::TimestampType();
//     case "DATE": return types::DateType();
//     case "TIME": return types::TimeType();
//     case "DATETIME": return types::DatetimeType();
//     case "GEOGRAPHY": return types::GeographyType();
//     case "STRUCT": return types::FloatType();
//     case "ARRAY": return types::FloatType();
//     default:
//       return nullptr;
//   }
// }

std::map<std::string, TypeKind> FromBigQueryTypeToZetaSQLTypeMap = {
  {"STRING", TYPE_STRING},
  {"INT64", TYPE_INT64},
  {"INTEGER", TYPE_INT64},
  {"BOOL", TYPE_BOOL},
  {"BOOLEAN", TYPE_BOOL},
  {"FLOAT64", TYPE_DOUBLE},
  {"NUMERIC", TYPE_NUMERIC},
  {"BYTES", TYPE_BYTES},
  {"TIMESTAMP", TYPE_TIMESTAMP},
  {"DATE", TYPE_DATE},
  {"TIME", TYPE_TIME},
  {"DATETIME", TYPE_DATETIME},
  {"GEOGRAPHY", TYPE_GEOGRAPHY},
  {"STRUCT", TYPE_STRUCT},
  {"ARRAY", TYPE_ARRAY},
};

void UpdateCatalogFromJSON(const std::string& json_schema_path, SimpleCatalog* catalog) {
  if (!std::filesystem::is_regular_file(json_schema_path)) {
    std::cout << "ERROR: not a json file path " << json_schema_path << std::endl;
    return;
  }
//   std::filesystem::path json_path(json_schema_path);
//   std::ifstream json_stream(json_path, std::ios::in);
//   std::string json(std::istreambuf_iterator<char>(json_stream), {});
//   std::stringstream json_string_stream(json);
//   BOOST_FOREACH (const ptree::value_type& child, pt.get_child("schemas")) {
//     const ptree& info = child.second;

//     // Data.info.id
//     if (boost::optional<int> id = info.get_optional<int>("id")) {
//       std::cout << "id : " << id.get() << std::endl;
//     }
//     else {
//       std::cout << "id is nothing" << std::endl;
//     }

//     // Data.info.name
//     if (boost::optional<std::string> name = info.get_optional<std::string>("name")) {
//       std::cout << "name : " << name.get() << std::endl;
//     } else {
//       std::cout << "name is nothing" << std::endl;
//     }
//   }

  using namespace boost;
  property_tree::ptree pt;
  property_tree::read_json(json_schema_path, pt);
  BOOST_FOREACH(property_tree::ptree::value_type &schema, pt.get_child("table_shecmas")) {
    std::string table_name = schema.second.get<std::string>("name");
    std::unique_ptr<SimpleTable> table(new SimpleTable(table_name));
    BOOST_FOREACH(property_tree::ptree::value_type &field, schema.second.get_child("schema")) {
      std::unique_ptr<SimpleColumn> column(
        new SimpleColumn(table_name, field.second.get<std::string>("name"),
                         types::TypeFromSimpleTypeKind(FromBigQueryTypeToZetaSQLTypeMap[field.second.get<std::string>("type")])));
      table->AddColumn(column.release(), true);
    }
    catalog->AddTable(table->Name(), table.release());
  }
//   for (;iter != iterEnd; ++iter) {
//     const std::string table_name = iter->first;
//     std::unique_ptr<SimpleTable> table(new SimpleTable(table_name));
//     for (auto& field : as_vector<std::map<std::string, std::string>>(pt, table_name)) {
//       std::unique_ptr<SimpleColumn> column(
//         new SimpleColumn(table_name, field["name"],
//                          types::TypeFromSimpleTypeKind(FromBigQueryTypeToZetaSQLTypeMap[field["type"]])));
//       table->AddColumn(column.release(), true);
//     }
//     catalog->AddTable(table->Name(), table.release());
//   }
  return;
}

}
