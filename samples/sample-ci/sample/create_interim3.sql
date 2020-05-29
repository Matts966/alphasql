CREATE OR REPLACE TABLE interim3 AS
SELECT
  x
FROM
  datawarehouse1
UNION ALL
SELECT
  x
FROM
  datawarehouse3;
