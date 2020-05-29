CREATE OR REPLACE TABLE datawarehouse1 AS
SELECT
  wban_number AS x
FROM
  `bigquery-public-data.samples.gsod`;
