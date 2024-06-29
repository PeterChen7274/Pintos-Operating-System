#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"
#include <stdlib.h>
#include <string.h>

static char buffer[512 * 20];
static char read_buffer_first[512 * 20];
static char read_buffer_second[512 * 20];

void test_main(void) {
  int fd;
  const char* file_name = "test";

  CHECK(create(file_name, 512 * 20), "create test file");
  CHECK((fd = open(file_name)) > 1, "open test file");

  memset(buffer, 'a', 512 * 20);
  CHECK(write(fd, buffer, 512 * 20) == 512 * 20, "write one buffer to file");
  close(fd);

  buffer_cache_reset(); // implemented this new function just for this test
  reset_buffer_cache_stats();

  CHECK((fd = open(file_name)) > 1, "reopen test file for first (cold) cache read");
  read(fd, read_buffer_first, 512 * 20);
  int cold_cache_hit_rate = get_buffer_cache_hit_rate();
  close(fd);

  reset_buffer_cache_stats();

  CHECK((fd = open(file_name)) > 1, "reopen test file for second cache read");
  read(fd, read_buffer_second, 512 * 20);
  int hot_cache_hit_rate = get_buffer_cache_hit_rate();
  close(fd);

  CHECK(hot_cache_hit_rate > cold_cache_hit_rate, "cache hit rate improved for second read");
}
