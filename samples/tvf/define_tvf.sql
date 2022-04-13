CREATE OR REPLACE TABLE FUNCTION `dataset.tvf`(input int64)
AS
  SELECT
    *
      EXCEPT (
        test_code
      )
  FROM
    UNNEST(ARRAY[STRUCT("1" AS id, "apple" AS product_name, 120 AS price, date("2022-01-11") AS purchase_date,
      1 AS test_code), ("2", "banana", 100, date("2022-01-11"), 1), ("3", "orange", 80, date("2022-01-12"),
      1)])
  WHERE
    test_code = input;
