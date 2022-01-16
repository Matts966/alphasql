CREATE OR REPLACE TABLE input AS
SELECT
  x
FROM
  datawarehouse2
UNION ALL
SELECT
  x
FROM
  datawarehouse3;
