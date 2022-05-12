#!/bin/bash
LD_LIBRARY_PATH=. valgrind --leak-check=yes ./actor_test_main.out
