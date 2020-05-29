CREATE OR REPLACE TABLE datawarehouse3 AS
SELECT
  year AS x
FROM
  `bigquery-public-data.samples.gsod`;
