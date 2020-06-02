UPDATE interim1
SET
  x = new_x.x
FROM
  (
    SELECT
      x
    FROM
      interim1
  ) AS new_x
WHERE
  x = 0
