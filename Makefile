# ============================================================
# Systems Programming - Assignment 2
# Mini Shell + Multi-Threaded WAL Recovery Engine
# ============================================================

CC = gcc
CFLAGS = -Wall -Wextra -std=gnu11 -O2


# ------------------------------------------------------------
# Mini Shell
# ------------------------------------------------------------

SHELL_SOURCE = shell/bash_mini.c
SHELL_TARGET = shell/bash_mini


# ------------------------------------------------------------
# WAL Recovery Engine
# ------------------------------------------------------------

WAL_SOURCE = wal/wal_recovery.c
WAL_TARGET = wal/wal_recovery
WAL_FLAGS = -pthread


# ------------------------------------------------------------
# Default build
# ------------------------------------------------------------

all: shell wal


# ------------------------------------------------------------
# Individual builds
# ------------------------------------------------------------

# Mini shell
shell:
	$(CC) $(CFLAGS) $(SHELL_SOURCE) -o $(SHELL_TARGET)

# WAL - ass2 the chellenging 
wal:
	$(CC) $(CFLAGS) $(WAL_SOURCE) -o $(WAL_TARGET) $(WAL_FLAGS)


# ------------------------------------------------------------
# Strict compiler checks
# ------------------------------------------------------------

check-shell:
	$(CC) $(CFLAGS) -Werror $(SHELL_SOURCE) -o $(SHELL_TARGET)

check-wal:
	$(CC) $(CFLAGS) -Werror $(WAL_SOURCE) -o $(WAL_TARGET) $(WAL_FLAGS)


# ------------------------------------------------------------
# Tests
# ------------------------------------------------------------

test-shell: check-shell
	bash ./shell/tests/run_all.sh

# test-wal: check-wal
#  	./wal/tests/run_all.sh

 test: test-shell # test-wal


# ------------------------------------------------------------
# Cleanup
# ------------------------------------------------------------

clean:
	rm -f $(SHELL_TARGET)
	rm -f $(WAL_TARGET)
	rm -f accounts.txt wal/accounts.txt


.PHONY: all shell wal check-shell check-wal test-shell test-wal test clean

# SUPPORTED COMMANDS:
# make shell
# make wal
# 
# make test-shell
# make test-wal
# 
# make test
# make clean