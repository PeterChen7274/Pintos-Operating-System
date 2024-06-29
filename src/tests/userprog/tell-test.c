#include "tests/lib.h"
#include "tests/main.h"
#include <syscall.h>

void test_main(void) {
  int fd;
  char buf[100];
  int initial_pos;
  int final_pos;
  char* file_name = "sample.txt";

  CHECK((fd = open(file_name)) > 1, "open \"%s\"", file_name);
  initial_pos = tell(fd);
  msg("initial position: %u", initial_pos);

  read(fd, buf, sizeof(buf));

  final_pos = tell(fd);
  msg("final position after reading 100 bytes: %u", final_pos);
  close(fd);
}