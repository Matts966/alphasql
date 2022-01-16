CREATE OR REPLACE MODEL `tmp.ml_sample`
OPTIONS
  (model_type='linear_reg', input_label_cols=['label']) AS
SELECT
      *
FROM
  `input`;
