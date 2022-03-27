BEGIN
  CREATE OR REPLACE TABLE datawarehouse2 AS
  SELECT
    column1 AS x
  FROM
    tablename1;
  CREATE OR REPLACE TABLE datawarehouse1 AS
  SELECT
    column2 AS x
  FROM
    tablename2;
END;
CALL create_datawarehouse3();
