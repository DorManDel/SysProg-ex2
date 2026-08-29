/*
 * Dor Mandel , ID: 315313825
 * Assignment 2 - Systems Programming Course
 * Mini Bash Shell
 * -----------------------------------------
 *
 * The program implements a small interactive shell whose lifecycle is:
 *
 *   prompt -> read -> parse -> classify -> execute -> repeat
 *
 * Built-in commands are executed by the shell process itself. External
 * commands are resolved according to the assignment search policy and are
 * executed in a child process created with fork().
 */

/** COMMENT TAGS:
 *  WHY[]     - explains why a non-obvious implementation choice exists.
 *  STUDY[]   - captures a C/Linux concept worth remembering.
 *  EXAMPLE[] - preserves a useful execution example or edge case.
 *  README[]  - marks material that can later move into the design document.
 *  TODO[]    - temporary development note; remove before final submission.
 *
 * Function comments describe the responsibility and contract of a component,
 * rather than narrating its current implementation line-by-line. This keeps
 * the comments useful even if the implementation changes later.
 */

#pragma region NOTES + FILE_DOCUMENTATION

/*

Linux / POSIX                 Windows
────────────────────────────────────────
fork()                        CreateProcess()
exec*()                       CreateProcess() model
waitpid()                     WaitForSingleObject()
unistd.h                      not native Win32
sys/wait.h                    not native Win32

---
DIAGRAM:
                     USER
                       │
                       │ "ls -l /home"
                       ▼
              ┌─────────────────┐
              │   INPUT READER  │
              └────────┬────────┘
                       │
                       │ raw line
                       ▼
              ┌─────────────────┐
              │     PARSER      │
              └────────┬────────┘
                       │
                       │ argv[]
                       ▼
              ┌─────────────────┐
              │   CLASSIFIER    │
              └────────┬────────┘
                       │
             ┌─────────┴──────────┐
             │                    │
             ▼                    ▼
         INTERNAL              EXTERNAL
             │                    │
      ┌──────┴──────┐             ▼
      │             │       EXECUTABLE
     exit           cd        RESOLVER
      │             │             │
      │          chdir()          │
      │                     $HOME/command
      │                           │
      │                      if not found
      │                           ↓
      │                      /bin/command
      │                           │
      │                           ▼
      │                    PROCESS EXECUTOR
      │                           │
      │                         fork()
      │                       /        \
      │                    CHILD       PARENT
      │                      │            │
      │                    exec()       wait()
      │                      │            │
      │                    program      status
      │                       \           /
      │                        └────┬─────┘
      │                             │
      └─────────────────────────────┘
                                    │
                                    ▼
                              NEXT PROMPT

---

*/

/*
PSEUDO CODE:
-----------

START

obtain HOME directory

shell_running = true


WHILE shell_running

    display prompt


    READ one line

    IF read was interrupted
        retry

    IF EOF
        stop shell

    IF read failed
        report error
        decide whether shell can continue


    PARSE line in-place into argv


    IF no tokens
        continue to next prompt


    CLASSIFY argv[0]


    IF command is EXIT

        shell_running = false


    ELSE IF command is CD

        validate directory argument

        call chdir()

        IF chdir failed
            report kernel error


    ELSE

        RESOLVE executable

        try:
            HOME/command

        IF not executable
            try:
                /bin/command


        IF neither is executable

            print:
            [command]: Unknown Command

            continue


        CALL fork()


        IF fork failed

            report kernel error
            continue


        IF child process

            CALL exec using:
                resolved executable
                argv

            IF execution continues here
                exec failed

            call perror()

            terminate child with failure


        ELSE parent process

            wait for exact child

            IF wait was interrupted
                retry

            IF wait failed
                report error

            ELSE
                inspect child termination status
                report return code


END WHILE


clean up reusable resources

EXIT

---

*/

/*

* STDIN   = fd 0
* STDOUT  = fd 1
* STDERR  = fd 2

---

| Operation              | Tool                | Important result                   |
| ---------------------- | ------------------- | ---------------------------------- |
| Read command           | `read()`            | `>0` bytes, `0` EOF, `-1` error    |
| Change shell directory | `chdir()`           | `0` success, `-1` failure          |
| Verify executable      | `access(..., X_OK)` | `0` executable, `-1` otherwise     |
| Create child           | `fork()`            | `<0` error, `0` child, `>0` parent |
| Replace child program  | `execv()`           | success never returns              |
| Wait for child         | `waitpid()`         | child PID or `-1`                  |
| Report kernel failures | `perror()`          | uses current `errno`               |

CONSTRUCTION:

┌─────────────────────────────┐
│ Libraries                   │
├─────────────────────────────┤
│ Constants / macros          │
├─────────────────────────────┤
│ Types                       │
├─────────────────────────────┤
│ Function declarations       │
├─────────────────────────────┤
│ Input component             │
├─────────────────────────────┤
│ Parser component            │
├─────────────────────────────┤
│ Built-in component          │
├─────────────────────────────┤
│ Executable resolver         │
├─────────────────────────────┤
│ Process executor            │
├─────────────────────────────┤
│ Shell controller            │
├─────────────────────────────┤
│ main()                      │
└─────────────────────────────┘
*/

#pragma endregion

#pragma region LIBRARIES + DEFINITIONS

/*
-----------------------------------------
Libraries
-----------------------------------------
*/

#include <errno.h>     /* errno, EINTR */
#include <limits.h>    /* PATH_MAX */
#include <stdbool.h>   /* bool, true, false */
#include <stdio.h>     /* fprintf(), perror(), printf(), snprintf(), stderr */
#include <stdlib.h>    /* EXIT_SUCCESS, EXIT_FAILURE, getenv() */
#include <string.h>    /* strcmp(), strlen() */
#include <sys/types.h> /* pid_t */
#include <sys/wait.h>  /* waitpid(), WIFEXITED(), WEXITSTATUS(), WIFSIGNALED() */
#include <unistd.h>    /* read(), write(), access(), chdir(), fork(), execv(), _exit() */

/*
-----------------------------------------
Constants / macros
-----------------------------------------
*/

// for one source of truth + readable naming + eawsy change

/* for easier write and read */
#define PROMPT "bash-mini$ "
#define BIN_DIRECTORY "/bin"

/* Reusable buffers keep the shell allocation-free during the command loop. */
#define INPUT_CHUNK_SIZE 4096
#define MAX_COMMAND_LEN 4096
/* for num of ptrs : [0]...[127] for instance - last is null */
#define MAX_ARGS 128

/* HELPER DEFINITIONS - POSIX convention */
#define FUNC_SUCCESS 0
#define FUNC_FAILED (-1)

// #define RESOLVE_FOUND 1
// #define RESOLVE_NOT_FOUND 0
// #define RESOLVE_ERROR (-1)

/* string has 1 char - NUL - at the end... */
// #define NUL_TRMNTR '\0'

/*
---
NUL   = '\0'   character with value zero
NULL  = null pointer constant
---

 '...' = capture extra arguments
 '__VA_ARGS__' = Everything passed into ... gets substituted there.(wrapper-ish)
*/

/*
 * WHY[ERROR_CHANNEL]:
 * Shell-generated diagnostics are written to stderr so normal command output
 * remains separate from errors and can still be redirected independently.
 */
#define ERROR_MSG(...)                \
    do                                \
    {                                 \
        fprintf(stderr, __VA_ARGS__); \
    } while (0)

/*
 * WHY[PERROR_MACRO]:
 * perror() prints the supplied context followed by the human-readable
 * description associated with the current errno value. It should be used
 * immediately after a failing errno-based operation.
 */
#define SYSTEM_ERROR(message) \
    do                        \
    {                         \
        perror(message);      \
    } while (0)

/*
ERRNO CONCEPT:
    ---
syscall - success
        - failure - errno
    ---
* examples:
read(), chdir(), fork(), waitpid(), execv()
    ---
ENOENT   No such file or directory
EACCES   Permission denied
EINTR    Interrupted system call
ENOMEM   Not enough memory
*/

#pragma endregion

#pragma region TYPES + DECLERATIONS

/*
-----------------------------------------
Types
-----------------------------------------
*/

/* INPUT STATUSES */
typedef enum
{
    INPUT_OK,
    INPUT_EOF,
    INPUT_TOO_LONG,
    INPUT_ERROR
} InputStatus;

/* COMMAND TYPES */
typedef enum
{
    CMD_EMPTY,
    CMD_EXIT,
    CMD_CD,
    CMD_EXTERNAL
} CommandType;

/* RESOLVE RETURN VALUES - instead of Macro */
typedef enum
{
    RESOLVE_ERROR = -1,
    RESOLVE_NOT_FOUND = 0,
    RESOLVE_FOUND = 1
} ResolveStatus;

/*
 * Holds unread bytes already obtained from STDIN.
 *
 * WHY[BUFFERED_INPUT]:
 * Reading one byte with one read() call would create an unnecessary system-call
 * boundary for every character. This cache lets one read() obtain many bytes
 * while still returning exactly one logical command line to the shell.
 */
typedef struct
{
    char data[INPUT_CHUNK_SIZE];
    size_t position;
    size_t length;
} InputReader;

/*
-----------------------------------------
Function declarations
-----------------------------------------
*/

static int write_all(int fd, const char *buffer, size_t count);
static InputStatus read_command_line(InputReader *reader, char *line, size_t line_capacity);
static int parse_command(char *line, char **argv, size_t argv_capacity);
static CommandType classify_command(int argc, char *const argv[]);
static int handle_cd(int argc, char *const argv[]);
static int build_path(char *destination, size_t destination_size,
                      const char *directory, const char *command);
static ResolveStatus resolve_executable(const char *command, const char *home,
                              char *resolved_path, size_t resolved_size);
static int wait_for_child(pid_t child_pid, int *status);
static void print_child_status(const char *command, int status);
static int execute_external(const char *path, char *const argv[]);
static int run_shell(void);
static int print_prompt(void);

#pragma endregion

#pragma region COMPONENTS

/*
-----------------------------------------
Output component
-----------------------------------------
*/

/**
 * @brief Write an entire buffer to a file descriptor.
 *
 * write() is allowed to complete only part of a request and may be interrupted
 * by a signal. This helper centralizes the robust write behavior used by the
 * shell prompt.
 *
 * @param fd      Destination file descriptor.
 * @param buffer  Bytes to write.
 * @param count   Number of bytes requested.
 *
 * @return FUNC_SUCCESS when every byte was written, FUNC_FAILED otherwise.
 */
static int write_all(int fd, const char *buffer, size_t count)
{
    size_t written_total = 0;

    while (written_total < count)
    {
        ssize_t written = write(fd, buffer + written_total, count - written_total);

        if (written < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            return FUNC_FAILED;
        }

        /* A zero-length write would make no progress, so treat it as failure. */
        if (written == 0)
        {
            errno = EIO;
            return FUNC_FAILED;
        }

        written_total += (size_t)written;
    }

    return FUNC_SUCCESS;
}

/*
-----------------------------------------
Input component
-----------------------------------------
*/

/**
 * @brief Read one logical command line from standard input.
 *
 * The function uses a reusable internal cache so a single read() may receive
 * several commands when input is redirected from a file or pipe. Unconsumed
 * bytes remain in the cache for the next call.
 *
 * The returned line excludes the trailing newline and is always NUL-terminated
 * when INPUT_OK is returned.
 *
 * @param reader         Persistent input-reader state.
 * @param line           Destination buffer for one command line.
 * @param line_capacity  Size of the destination buffer in bytes.
 *
 * @return INPUT_OK for one complete command.
 * @return INPUT_EOF when no additional command remains.
 * @return INPUT_TOO_LONG when the command exceeded the destination capacity.
 * @return INPUT_ERROR for a non-recoverable read() failure.
 */
static InputStatus read_command_line(InputReader *reader, char *line, size_t line_capacity)
{
    size_t line_length = 0;
    bool overflow = false;

    if (reader == NULL || line == NULL || line_capacity == 0)
    {
        errno = EINVAL;
        return INPUT_ERROR;
    }

    for (;;)
    {
        if (reader->position == reader->length)
        {
            ssize_t bytes_read;

            do
            {
                bytes_read = read(STDIN_FILENO, reader->data, sizeof(reader->data));
            } while (bytes_read < 0 && errno == EINTR);

            if (bytes_read < 0)
            {
                return INPUT_ERROR;
            }

            if (bytes_read == 0)
            {
                if (line_length == 0 && !overflow)
                {
                    return INPUT_EOF;
                }

                /* EOF can terminate the final command even without '\n'. */
                if (!overflow)
                {
                    if (line_length > 0 && line[line_length - 1] == '\r')
                    {
                        line_length--;
                    }
                    line[line_length] = '\0';
                    return INPUT_OK;
                }

                return INPUT_TOO_LONG;
            }

            reader->position = 0;
            reader->length = (size_t)bytes_read;
        }

        while (reader->position < reader->length)
        {
            char current = reader->data[reader->position++];

            if (current == '\n')
            {
                if (overflow)
                {
                    return INPUT_TOO_LONG;
                }

                /* Also tolerate CRLF input files created on Windows. */
                if (line_length > 0 && line[line_length - 1] == '\r')
                {
                    line_length--;
                }

                line[line_length] = '\0';
                return INPUT_OK;
            }

            if (!overflow)
            {
                if (line_length + 1 < line_capacity)
                {
                    line[line_length++] = current;
                }
                else
                {
                    /* Continue consuming until newline so the next read starts cleanly. */
                    overflow = true;
                }
            }
        }
    }
}

/*
-----------------------------------------
Parser component
-----------------------------------------
*/

/**
 * @brief Split one command line into an argv-compatible token array.
 *
 * The parser modifies the original line in place. Delimiters are replaced with
 * '\0', and argv entries point directly into that same buffer. This avoids
 * allocating and copying a second string for every token.
 *
 * Only spaces and tabs are treated as separators, matching the assignment
 * specification. Quoting, escaping, pipes, and redirection are intentionally
 * outside the required mini-shell grammar.
 *
 * @param line           Mutable command-line buffer.
 * @param argv           Output array of token pointers.
 * @param argv_capacity  Number of entries available in argv.
 *
 * @return Number of parsed arguments on success, FUNC_FAILED if argv is full.
 */
static int parse_command(char *line, char **argv, size_t argv_capacity)
{
    size_t argc = 0;
    char *cursor = line;

    if (line == NULL || argv == NULL || argv_capacity == 0)
    {
        return FUNC_FAILED;
    }

    while (*cursor != '\0')
    {
        while (*cursor == ' ' || *cursor == '\t')
        {
            *cursor = '\0';
            cursor++;
        }

        if (*cursor == '\0')
        {
            break;
        }

        /* Reserve one final slot for the NULL terminator required by execv(). */
        if (argc + 1 >= argv_capacity)
        {
            argv[0] = NULL;
            return FUNC_FAILED;
        }

        argv[argc++] = cursor;

        while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t')
        {
            cursor++;
        }
    }

    argv[argc] = NULL;
    return (int)argc;
}

/*
-----------------------------------------
Command Classifier
-----------------------------------------
*/

/**
 * @brief Classify a parsed command by the behavior the shell must own.
 *
 * The classifier separates shell-owned state changes from external program
 * execution. `exit` and `cd` must be handled in the shell process; every other
 * command is delegated to the external-command path.
 *
 * @param argc  Number of parsed tokens.
 * @param argv  Parsed token array.
 *
 * @return Command type used by the shell controller.
 */
static CommandType classify_command(int argc, char *const argv[])
{
    if (argc <= 0 || argv == NULL || argv[0] == NULL)
    {
        return CMD_EMPTY;
    }

    if (strcmp(argv[0], "exit") == 0)
    {
        return CMD_EXIT;
    }

    if (strcmp(argv[0], "cd") == 0)
    {
        return CMD_CD;
    }

    return CMD_EXTERNAL;
}

/*
-----------------------------------------
Built-in component
-----------------------------------------
*/

/**
 * @brief Execute the shell-owned `cd` command.
 *
 * Changing directory must happen in the shell process itself. If a child
 * changed its directory instead, that state would disappear when the child
 * terminated and the parent shell would remain in its previous directory.
 *
 * This implementation requires exactly one directory argument because the
 * assignment defines directory change behavior but does not require the extra
 * convenience semantics of a full Bash implementation.
 *
 * @param argc  Number of parsed tokens.
 * @param argv  Parsed token array beginning with "cd".
 *
 * @return FUNC_SUCCESS on successful directory change, FUNC_FAILED otherwise.
 */
static int handle_cd(int argc, char *const argv[])
{
    if (argc != 2)
    {
        ERROR_MSG("Usage: cd <directory>\n");
        return FUNC_FAILED;
    }

    /*
     * STUDY[CHDIR]:
     * chdir() updates the current working directory stored for this process.
     * A return value of -1 indicates failure and sets errno.
     */
    if (chdir(argv[1]) < 0)
    {
        SYSTEM_ERROR("cd");
        return FUNC_FAILED;
    }

    return FUNC_SUCCESS;
}

/*
-----------------------------------------
Executable resolver
-----------------------------------------
*/

/**
 * @brief Build `<directory>/<command>` inside a caller-owned buffer.
 *
 * @param destination       Output path buffer.
 * @param destination_size  Capacity of the output buffer.
 * @param directory         Directory to search.
 * @param command           Command file name.
 *
 * @return FUNC_SUCCESS when the complete path fits, FUNC_FAILED otherwise.
 */
static int build_path(char *destination, size_t destination_size,
                      const char *directory, const char *command)
{
    int written;

    if (destination == NULL || directory == NULL || command == NULL)
    {
        return FUNC_FAILED;
    }

    written = snprintf(destination, destination_size, "%s/%s", directory, command);

    if (written < 0 || (size_t)written >= destination_size)
    {
        return FUNC_FAILED;
    }

    return FUNC_SUCCESS;
}

/**
 * @brief Resolve an external command according to the required search order.
 *
 * Search policy:
 *   1. `$HOME/<command>`
 *   2. `/bin/<command>`
 *
 * A candidate is accepted only when access(..., X_OK) confirms that it is
 * executable by the current process. The later execv() call remains the final
 * authority because the filesystem can change between lookup and execution.
 *
 * @param command        Command name from argv[0].
 * @param home           Value of the HOME environment variable, or NULL.
 * @param resolved_path  Output buffer receiving the executable path.
 * @param resolved_size  Capacity of the output path buffer.
 *
 * @return RESOLVE_FOUND when an executable was found.
 * @return RESOLVE_NOT_FOUND when neither required location is executable.
 * @return RESOLVE_ERROR when a candidate path cannot be represented safely.
 */
static ResolveStatus resolve_executable(const char *command, const char *home,
                                        char *resolved_path, size_t resolved_size)
{
    if (command == NULL || resolved_path == NULL || resolved_size == 0)
    {
        return RESOLVE_ERROR;
    }

    /* takes care also for tooLong path - path cannot be represented */
    if (home != NULL && *home != '\0')
    {
        if (build_path(resolved_path,
                       resolved_size,
                       home,
                       command) == FUNC_FAILED)
        {
            return RESOLVE_ERROR;
        }

        if (access(resolved_path, X_OK) == 0)
        {
            return RESOLVE_FOUND;
        }
    }

    if (build_path(resolved_path, resolved_size, BIN_DIRECTORY, command) == FUNC_FAILED)
    {
        return RESOLVE_ERROR;
    }

    if (access(resolved_path, X_OK) == 0)
    {
        return RESOLVE_FOUND;
    }

    return RESOLVE_NOT_FOUND;
}

/*
-----------------------------------------
Process executor
-----------------------------------------
*/

/**
 * @brief Wait for one specific child process, retrying if interrupted.
 *
 * @param child_pid  PID returned to the parent by fork().
 * @param status     Output location for the encoded child termination status.
 *
 * @return FUNC_SUCCESS when the requested child was collected.
 * @return FUNC_FAILED on a non-recoverable waitpid() failure.
 */
static int wait_for_child(pid_t child_pid, int *status)
{
    pid_t result;

    do
    {
        result = waitpid(child_pid, status, 0);
    } while (result < 0 && errno == EINTR);

    if (result < 0)
    {
        return FUNC_FAILED;
    }

    return FUNC_SUCCESS;
}

/**
 * @brief Report how an external command terminated.
 *
 * waitpid() stores an encoded status rather than the program's return code
 * directly. The WIF* and WEXITSTATUS macros must be used to interpret it.
 *
 * @param command  User command name.
 * @param status   Encoded wait status returned by waitpid().
 */
static void print_child_status(const char *command, int status)
{
    if (WIFEXITED(status))
    {
        int return_code = WEXITSTATUS(status);

        if (return_code == EXIT_SUCCESS)
        {
            printf("Command [%s] executed successfully. Return code: %d\n",
                   command, return_code);
        }
        else
        {
            printf("Command [%s] finished. Return code: %d\n",
                   command, return_code);
        }
    }
    else if (WIFSIGNALED(status))
    {
        printf("Command [%s] terminated by signal: %d\n",
               command, WTERMSIG(status));
    }

    /* Keep status output ordered before the next prompt even when stdout is piped. */
    fflush(stdout);
}

/**
 * @brief Execute one already-resolved external program.
 *
 * fork() creates the child execution context. The child replaces its shell
 * program image with the requested executable using execv(). The parent keeps
 * running the shell and blocks in waitpid() until that exact child terminates.
 *
 * @param path  Resolved executable path.
 * @param argv  NULL-terminated argument vector passed to execv().
 *
 * @return FUNC_SUCCESS after the child has been collected.
 * @return FUNC_FAILED when fork() or waitpid() fails in the parent.
 */
static int execute_external(const char *path, char *const argv[])
{
    pid_t child_pid;
    int status = 0;

    /*
     * STUDY[FORK_RETURN]:
     *   < 0 : process creation failed
     *   = 0 : current execution path is the child
     *   > 0 : current execution path is the parent; value is the child PID
     */
    child_pid = fork();

    if (child_pid < 0)
    {
        SYSTEM_ERROR("fork");
        return FUNC_FAILED;
    }

    if (child_pid == 0)
    {
        /*
         * STUDY[EXEC_REPLACES_PROGRAM]:
         * execv() does not create another process. On success it replaces the
         * child process image, so this function never returns in the child.
         */
        execv(path, argv);

        /* Reaching this point means execv() failed; the assignment requires perror(). */
        SYSTEM_ERROR("execv");

        /*
         * WHY[_EXIT_IN_CHILD]:
         * _exit() terminates the child immediately without running inherited
         * stdio cleanup that belongs to the parent shell process.
         */
        _exit(EXIT_FAILURE);
    }

    if (wait_for_child(child_pid, &status) == FUNC_FAILED)
    {
        SYSTEM_ERROR("waitpid");
        return FUNC_FAILED;
    }

    print_child_status(argv[0], status);
    return FUNC_SUCCESS;
}

#pragma endregion

/*
-----------------------------------------
Shell controller
-----------------------------------------
*/

/**
 * @brief Run the complete Mini Bash lifecycle until `exit` or end-of-input.
 *
 * The controller owns the reusable buffers and coordinates the specialized
 * components. Parsing, built-in behavior, executable lookup, and process
 * execution remain separate so each responsibility can be reasoned about and
 * tested independently.
 *
 * @return EXIT_SUCCESS for a normal shell shutdown, EXIT_FAILURE if the shell
 *         itself encounters an unrecoverable input/output failure.
 */
static int run_shell(void)
{
    InputReader reader = {0};
    char command_line[MAX_COMMAND_LEN];
    char *argv[MAX_ARGS];
    char resolved_path[PATH_MAX];
    const char *home = getenv("HOME");
    bool shell_running = true;

    while (shell_running)
    {
        InputStatus input_status;
        int argc;
        CommandType command_type;

        // if (write_all(STDOUT_FILENO, PROMPT, strlen(PROMPT)) == FUNC_FAILED)
        // {
        //     SYSTEM_ERROR("write prompt");
        //     return EXIT_FAILURE;
        // }
        if (print_prompt() == FUNC_FAILED)
        {
            SYSTEM_ERROR("write prompt");
            return EXIT_FAILURE;
        }

        input_status = read_command_line(&reader, command_line, sizeof(command_line));

        if (input_status == INPUT_EOF)
        {
            /* Print a newline so an interactive Ctrl-D leaves the terminal tidy. */
            (void)write_all(STDOUT_FILENO, "\n", 1);
            break;
        }

        if (input_status == INPUT_TOO_LONG)
        {
            ERROR_MSG("Error: command exceeds %d bytes.\n", MAX_COMMAND_LEN - 1);
            continue;
        }

        if (input_status == INPUT_ERROR)
        {
            SYSTEM_ERROR("read");
            return EXIT_FAILURE;
        }

        argc = parse_command(command_line, argv, MAX_ARGS);

        if (argc == FUNC_FAILED)
        {
            ERROR_MSG("Error: too many command arguments (maximum %d).\n", MAX_ARGS - 1);
            continue;
        }

        command_type = classify_command(argc, argv);

        switch (command_type)
        {
        case CMD_EMPTY:
            break;

        case CMD_EXIT:
            shell_running = false;
            break;

        case CMD_CD:
            (void)handle_cd(argc, argv);
            break;

        case CMD_EXTERNAL:
        {
            ResolveStatus resolve_result = resolve_executable(
                argv[0], home, resolved_path, sizeof(resolved_path));

            if (resolve_result == RESOLVE_ERROR)
            {
                ERROR_MSG("Error: executable path is too long.\n");
                break;
            }

            if (resolve_result == RESOLVE_NOT_FOUND)
            {
                ERROR_MSG("[%s]: Unknown Command\n", argv[0]);
                break;
            }

            (void)execute_external(resolved_path, argv);
            break;
        }
        }
    }

    return EXIT_SUCCESS;
}

/* PRINT PROMPT HELPER */
static int print_prompt(void)
{
    return write_all(
        STDOUT_FILENO,
        PROMPT,
        strlen(PROMPT)
    );
}

/*
-----------------------------------------
main()
-----------------------------------------
*/

/**
 * @brief Program entry point.
 *
 * main() delegates the shell lifecycle to the controller so the entry point
 * remains small and contains no command-processing policy of its own.
 */
int main(void)
{
    return run_shell();
}

/*
bash-mini$ ls -l
bash-mini$ pwd
bash-mini$ cd /tmp
bash-mini$ pwd
bash-mini$ this_does_not_exist
bash-mini$ exit

ls          → external → /bin/ls → fork → exec → wait
pwd         → external → /bin/pwd → fork → exec → wait
cd /tmp     → internal → chdir → NO fork
pwd         → should now print /tmp
unknown     → HOME miss → /bin miss → Unknown Command
exit        → internal → stop loop → NO fork
*/