#include "tests/userprog/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
  int fd;
  char* file_name = "sample.txt";
  CHECK((fd = open(file_name)) > 1, "open \"%s\" for verification", file_name);
  check_file_handle(fd, file_name, sample, sizeof sample - 1);
  seek(fd, 0);
  check_file_handle(fd, file_name, sample, sizeof sample - 1);
  msg("close \"%s\"", file_name);
  close(fd);
}