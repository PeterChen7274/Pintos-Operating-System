# -*- perl -*-
use strict;
use warnings;
use tests::tests;

check_expected(IGNORE_EXIT_CODES => 1, [<<'EOF']);
(coalesce) begin
(coalesce) create filename
(coalesce) open filename
(coalesce) writing
(coalesce) reading
(coalesce) write count check
(coalesce) done
(coalesce) end
EOF

pass;