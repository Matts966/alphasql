UPDATE interim1
SET
  x = new_x.xx
FROM
  (
    SELECT
      x as xx
    FROM
      interim1
  ) AS new_x
WHERE
  x = 0;
