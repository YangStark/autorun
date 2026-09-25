#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p tests/.build
gcc -std=c11 -Wall -Wextra -Werror -O2 -Itests \
  tests/deploy_host_test.c loader/deploy.c \
  /lib/x86_64-linux-gnu/libcrypto.so.3 -o tests/.build/deploy_host_test
tests/.build/deploy_host_test
