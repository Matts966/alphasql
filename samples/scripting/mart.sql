BEGIN TRANSACTION;

CREATE TEMP TABLE tmp AS
SELECT * FROM datawarehouse1
UNION ALL
SELECT * FROM datawarehouse2
UNION ALL
SELECT * FROM datawarehouse3;

DROP TABLE IF EXISTS `datawarehouse1`;
DROP TABLE IF EXISTS `datawarehouse2`;
DROP TABLE IF EXISTS `datawarehouse1`;

CREATE OR REPLACE TABLE mart AS
SELECT * FROM tmp;

COMMIT TRANSACTION;
