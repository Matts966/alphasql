BEGIN
  CREATE OR REPLACE TABLE datawarehouse2 AS
  SELECT
    column1 AS x
  FROM
    tablename1;
  CREATE OR REPLACE TABLE datawarehouse1 AS
  SELECT
    CAST(column2 AS INT64) AS x
  FROM
    tablename2;
  CALL create_datawarehouse3();
END
