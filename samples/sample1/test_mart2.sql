WITH
  row_count AS (
    SELECT
      row_count
    FROM
      dataset.__TABLES__
    WHERE
      table_id = mart
  )
SELECT
  `IF`((
      SELECT
        COUNT(DISTINCT x) = row_count.row_count
      FROM
        mart
    ), ERROR("x should be unique"), "OK");
