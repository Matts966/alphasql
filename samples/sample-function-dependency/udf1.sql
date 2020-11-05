CREATE FUNCTION multiplyInputs(x # comment
FLOAT64, y FLOAT64)
RETURNS FLOAT64 LANGUAGE js AS """
  return x*y;
""";
-- comment1
-- comment2
WITH
  numbers AS (
    SELECT
      1 AS x,
      5 AS y
    UNION ALL
    SELECT
      2 AS x,
      10 AS y
    UNION ALL
    SELECT
      3 AS x,
      15 AS y
  ) /* comment! */
SELECT
  x,
  y,
  multiplyInputs(divideByTwo(x), divideByTwo(y)) AS half_product
FROM
  numbers;
