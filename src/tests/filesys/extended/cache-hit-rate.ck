# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(cache-hit-rate) begin
(cache-hit-rate) create test file
(cache-hit-rate) open test file
(cache-hit-rate) write one buffer to file
(cache-hit-rate) reopen test file for first (cold) cache read
(cache-hit-rate) reopen test file for second cache read
(cache-hit-rate) cache hit rate improved for second read
(cache-hit-rate) end
EOF
pass;
