SELECT
  `IF`((
      SELECT
        COUNT(*) = 0
      FROM
        mart
    ), ERROR("no data"), "OK");
