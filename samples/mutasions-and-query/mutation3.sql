UPDATE `dataset.main`
SET x = 5;

CREATE OR REPLACE TABLE warehouse
AS SELECT *
FROM `dataset.main`;
