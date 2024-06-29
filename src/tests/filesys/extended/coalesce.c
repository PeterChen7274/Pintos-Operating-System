#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

#define FILE_SIZE 65536

void test_main(void) {
  char data = 'a';
  int fd, i;

  CHECK(create("filename", 0), "create filename");
  CHECK((fd = open("filename")) > 1, "open filename");

  msg("writing");
  for (i = 0; i < FILE_SIZE; i++) {
    write(fd, &data, 1);
  }

  buffer_cache_reset();

  seek(fd, 0);
  msg("reading");
  for (i = 0; i < FILE_SIZE; i++) {
    read(fd, &data, 1);
  }

  close(fd);

  // check write count
  CHECK(get_write_count() < 10 * 128, "write count check");
  // msg("write count: %d", get_write_count());
  // msg("read count: %d", get_read_count());

  msg("done");
}
