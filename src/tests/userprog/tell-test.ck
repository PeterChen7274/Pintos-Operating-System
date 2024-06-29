# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(tell-test) begin
(tell-test) open "sample.txt"
(tell-test) initial position: 0
(tell-test) final position after reading 100 bytes: 100
(tell-test) end
tell-test: exit(0)
EOF
pass;