CREATE OR REPLACE FUNCTION dedup_count(arr ANY TYPE)
AS (
  ARRAY_LENGTH(dedup(arr))
);
ASSERT dedup_count(ARRAY[]) = 0;
ASSERT dedup_count(ARRAY[1]) = 1;
ASSERT dedup_count(ARRAY[1, 2]) = 2;
ASSERT dedup_count(ARRAY[1, 3, 5]) = 3;
ASSERT dedup_count(ARRAY[1, 3, 3, 5, 5]) = 3;
