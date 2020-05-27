CREATE OR REPLACE TABLE datawarehouse1 AS
SELECT wban_number as x
FROM `bigquery-public-data.samples.gsod`;
