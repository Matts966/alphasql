WITH
  row_count AS (
    -- To avoid ERROR: INVALID_ARGUMENT: Table not found: dataset.__TABLES__
    --
    -- SELECT
    --   row_count
    -- FROM
    --   dataset.__TABLES__
    -- WHERE
    --   table_id = mart
    --
    SELECT
      COUNT(*)
    FROM
      mart
  ),
  row_count_unique AS (
    SELECT
      COUNT(DISTINCT x)
    FROM
      mart
  )
SELECT
  `IF`((
      SELECT
        row_count = row_count_unique
      FROM
        row_count,
        row_count_unique
    ), ERROR("x should be unique"), "OK");
