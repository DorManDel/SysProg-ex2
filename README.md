# Systems Programming - Assignment 2

## Mini Bash Shell

This project implements a small Bash-like command interpreter in C using Linux/POSIX system calls.

The shell follows the basic cycle:

```text
prompt -> read -> parse -> classify -> execute -> repeat
```

### Features

- Prompt: `bash-mini$`
- Built-in commands:
  - `cd`
  - `exit`
- External command execution
- Executable lookup order:
  1. `$HOME/<command>`
  2. `/bin/<command>`
- Uses `fork()` to create a child process
- Uses `execv()` to run external programs
- Uses `waitpid()` to wait for the child and inspect its termination status
- Handles invalid commands and system-call errors
- Buffered input handling
- Automated integration tests

## Build

```bash
make
```

or:

```bash
make shell
```

## Run

```bash
./shell/bash_mini
```

Example:

```text
bash-mini$ pwd
/home/user

bash-mini$ echo hello
hello

bash-mini$ cd /tmp

bash-mini$ exit
```

## Test

```bash
make test-shell
```

## Project Structure

```text
SysProg-ex2/
├── shell/
│   ├── bash_mini.c
│   └── tests/
│       └── run_all.sh
├── docs/
│   └── BASH_MINI_EXPLAINED.md
├── Makefile
└── README.md
```

For a detailed explanation of the design and implementation, see:

`docs/BASH_MINI_EXPLAINED.md`

---

The challenging WAL recovery part of Assignment 2 will be added separately.
