UPDATE `dataset.main`
SET x = 5
WHERE x = 0;

CREATE OR REPLACE TABLE warehouse
AS SELECT *
FROM `dataset.main`;
