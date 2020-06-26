CREATE TABLE `dataset.dependency` AS
SELECT
  "this table is intended to be deleted by DROP TABLE IF EXISTS statements in SQL files that reference a table after this query finished" AS explanation;
INSERT INTO `dataset.main`
SELECT
  1;
