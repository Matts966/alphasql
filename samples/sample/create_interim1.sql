CREATE OR REPLACE TABLE interim1 AS
SELECT
  x
FROM
  datawarehouse1
UNION ALL
SELECT
  x
FROM
  datawarehouse2;
-- insert for preprocessing
INSERT INTO interim1
SELECT
  x
FROM
  interim1;
