CREATE OR REPLACE TABLE datawarehouse3 AS
SELECT year as x
FROM `bigquery-public-data.samples.gsod`;
