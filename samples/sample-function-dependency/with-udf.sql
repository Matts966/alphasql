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
  )
SELECT
  x,
  y,
  multiplyInputs(divideByTwo(x), divideByTwo(y)) AS half_product
FROM
  numbers;
