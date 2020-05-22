CREATE OR REPLACE TABLE `interium2` AS
SELECT x FROM datawarehouse2
UNION ALL
SELECT x FROM datawarehouse3;
