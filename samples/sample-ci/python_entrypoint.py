#!/usr/bin/env python

import pip
import json


def install(package):
    if hasattr(pip, 'main'):
        pip.main(['install', package])
    else:
        pip._internal.main(['install', package])

def main():
    from google.cloud.bigquery import Client
    TABLE_SEP = '.'
    tables = {}
    with open('/vol/required_tables.txt') as rt:
        table_names = rt.read().split()
        bq_client = Client()
        for table_name in table_names:
            splited_table_name = table_name.split('.')
            if len(splited_table_name) == 3:
                dataset_ref = bq_client.dataset(splited_table_name[1], project=splited_table_name[0])
            else:
                dataset_ref = bq_client.dataset(splited_table_name[0])
            table_ref = dataset_ref.table(splited_table_name[-1])
            table = bq_client.get_table(table_ref)
            tables[table_name] = [field.to_api_repr() for field in table.schema]
    with open('/vol/schema.json', mode='w') as schema:
        schema.write(json.dumps(tables))


if __name__ == '__main__':
    install('google-cloud-bigquery')
    main()
