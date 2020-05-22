CREATE OR REPLACE TABLE `interium3` AS
SELECT x FROM datawarehouse1
UNION ALL
SELECT x FROM datawarehouse3;
