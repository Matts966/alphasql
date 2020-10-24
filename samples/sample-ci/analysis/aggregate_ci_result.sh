#!/bin/bash

CI_FILTER_STRING="triggerId=8005963a-d3a9-4358-ab0b-33dab35419d9 OR triggerId=79454ba2-833e-404e-821c-dfe261c59fbb"

mkdir -p logs
gcloud builds list --limit 3000 --page-size 3000 --filter="${CI_FILTER_STRING}" | cut -f 1 -d ' ' | while read id; do
    gcloud builds log $id > logs/$id.txt
done
rm logs/ID.txt
mkdir -p logs/success
gcloud builds list --limit 3000 --page-size 3000 --filter="(${CI_FILTER_STRING}) AND status=SUCCESS" | cut -f 1 -d ' ' | while read id; do
    mv logs/$id.txt logs/success/
done
CAUSE='no_data_error'
mkdir -p logs/$CAUSE
grep -l logs/*.txt -e "Not found: Table\|ERROR: INVALID_ARGUMENT: Table not found:\|Not found: Dataset" | while read file; do
    mv $file logs/$CAUSE/
done
CAUSE='syntax_error'
mkdir -p logs/$CAUSE
grep -l logs/*.txt -e "INVALID_ARGUMENT: Syntax error: \|Invalid dataset ID" | while read file; do
    mv $file logs/$CAUSE/
done
CAUSE='type_error'
mkdir -p logs/$CAUSE
grep -l logs/*.txt -e "ERROR: INVALID_ARGUMENT: No matching signature for \|ERROR: INVALID_ARGUMENT: Unrecognized name:\|ERROR: INVALID_ARGUMENT: Name .* not found" | while read file; do
    mv $file logs/$CAUSE/
done
CAUSE='cycle_error'
mkdir -p logs/$CAUSE
grep -l logs/*.txt -e "The graph must be a DAG." | while read file; do
    mv $file logs/$CAUSE/
done
CAUSE='duplicate_table_error'
mkdir -p logs/$CAUSE
grep -l logs/*.txt -e "duplicate key:" | while read file; do
    mv $file logs/$CAUSE/
done
mkdir -p logs/etc
mv logs/*.txt logs/etc

cat <<EOF | python
import openpyxl
wb = openpyxl.load_workbook("ci-logs.xlsx")
ws = wb.worksheets[0]
c_success = ws["B1"]
c_success.value = $(ls -1q logs/success/*.txt | wc -l)
c_error = ws["B2"]
c_error.value = $(ls -1q logs/etc/*.txt | wc -l)
c_no_data_error = ws["B4"]
c_no_data_error.value = $(ls -1q logs/no_data_error/*.txt | wc -l)
c_type_error = ws["B5"]
c_type_error.value = $(ls -1q logs/type_error/*.txt | wc -l)
c_syntax_error = ws["B6"]
c_syntax_error.value = $(ls -1q logs/syntax_error/*.txt | wc -l)
c_duplicate_table_error = ws["B7"]
c_duplicate_table_error.value = $(ls -1q logs/duplicate_table_error/*.txt | wc -l)
c_cycle_error = ws["B8"]
c_cycle_error.value = $(ls -1q logs/cycle_error/*.txt | wc -l)
wb.save("ci-logs.xlsx")
EOF
