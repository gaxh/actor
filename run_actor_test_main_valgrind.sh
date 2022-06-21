#!/bin/bash
LD_LIBRARY_PATH=. valgrind --leak-check=full ./actor_test_main.out
