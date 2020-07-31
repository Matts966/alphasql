SELECT
  `IF`((
      SELECT
        COUNT(*) > 0
      FROM
        mart
      WHERE
        x < 0
    ), ERROR("x should be positive"), "OK");
