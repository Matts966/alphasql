CREATE OR REPLACE TABLE datawarehouse2 AS
SELECT station_number as x
FROM `bigquery-public-data.samples.gsod`;
