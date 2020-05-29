CREATE OR REPLACE TABLE datawarehouse2 AS
SELECT
  station_number AS x
FROM
  `bigquery-public-data.samples.gsod`;
