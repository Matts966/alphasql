CREATE OR REPLACE TABLE `interium1` AS
SELECT x FROM datawarehouse1
UNION ALL
SELECT x FROM datawarehouse2;
