CREATE OR REPLACE PROCEDURE create_datawarehouse3()
BEGIN
  CREATE OR REPLACE TABLE datawarehouse3 AS
  SELECT
    x
  FROM
    dataset.main;
END
