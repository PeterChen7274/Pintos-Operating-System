# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(read-seek) begin
(read-seek) open "sample.txt" for verification
(read-seek) verified contents of "sample.txt"
(read-seek) verified contents of "sample.txt"
(read-seek) close "sample.txt"
(read-seek) end
read-seek: exit(0)
EOF
pass;