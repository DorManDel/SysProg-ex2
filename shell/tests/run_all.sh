#!/usr/bin/env bash
#
# Automated integration tests for the Assignment 2 Mini Bash shell.
#
# Run from the repository root with:
#   make test-shell
#
# Or directly:
#   bash shell/tests/run_all.sh
#
# The suite treats bash_mini as a black box: commands are piped into stdin,
# stdout/stderr are captured, and observable behavior is verified.
#

set -u
set -o pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
BIN="${REPO_ROOT}/shell/bash_mini"

PROMPT='bash-mini$ '

TMP_ROOT="$(mktemp -d)"
OUT_FILE="${TMP_ROOT}/stdout.txt"
ERR_FILE="${TMP_ROOT}/stderr.txt"

TOTAL=0
PASSED=0
FAILED=0
LAST_RC=0

if [[ -t 1 ]]; then
    GREEN=$'\033[0;32m'
    RED=$'\033[0;31m'
    CYAN=$'\033[0;36m'
    YELLOW=$'\033[0;33m'
    RESET=$'\033[0m'
else
    GREEN=''
    RED=''
    CYAN=''
    YELLOW=''
    RESET=''
fi

cleanup()
{
    rm -rf "${TMP_ROOT}"
}
trap cleanup EXIT

print_failure_context()
{
    printf '%s\n' "${YELLOW}----- captured stdout -----${RESET}"
    cat "${OUT_FILE}" 2>/dev/null || true
    printf '\n%s\n' "${YELLOW}----- captured stderr -----${RESET}"
    cat "${ERR_FILE}" 2>/dev/null || true
    printf '\n%s\n' "${YELLOW}---------------------------${RESET}"
}

run_shell()
{
    local input="$1"
    shift

    : > "${OUT_FILE}"
    : > "${ERR_FILE}"

    printf '%s' "${input}" |
        "$@" "${BIN}" >"${OUT_FILE}" 2>"${ERR_FILE}"

    LAST_RC=$?
}

assert_rc()
{
    local expected="$1"

    if [[ "${LAST_RC}" -ne "${expected}" ]]; then
        printf 'Expected shell exit code %s, got %s\n' "${expected}" "${LAST_RC}"
        return 1
    fi
}

assert_stdout_contains()
{
    local expected="$1"

    if ! grep -Fq -- "${expected}" "${OUT_FILE}"; then
        printf 'stdout does not contain: %q\n' "${expected}"
        return 1
    fi
}

assert_stdout_not_contains()
{
    local unexpected="$1"

    if grep -Fq -- "${unexpected}" "${OUT_FILE}"; then
        printf 'stdout unexpectedly contains: %q\n' "${unexpected}"
        return 1
    fi
}

assert_stderr_contains()
{
    local expected="$1"

    if ! grep -Fq -- "${expected}" "${ERR_FILE}"; then
        printf 'stderr does not contain: %q\n' "${expected}"
        return 1
    fi
}

assert_stderr_empty()
{
    if [[ -s "${ERR_FILE}" ]]; then
        printf 'Expected stderr to be empty.\n'
        return 1
    fi
}

assert_prompt_count()
{
    local expected="$1"
    local content
    local count=0

    content="$(cat "${OUT_FILE}")"

    while [[ "${content}" == *"${PROMPT}"* ]]; do
        content="${content#*"${PROMPT}"}"
        ((count++))
    done

    if [[ "${count}" -ne "${expected}" ]]; then
        printf 'Expected %s prompts, got %s\n' "${expected}" "${count}"
        return 1
    fi
}

run_test()
{
    local name="$1"
    shift

    ((TOTAL++))

    printf '%-58s' "${name}"

    if "$@"; then
        ((PASSED++))
        printf '%sPASS%s\n' "${GREEN}" "${RESET}"
    else
        ((FAILED++))
        printf '%sFAIL%s\n' "${RED}" "${RESET}"
        print_failure_context
    fi
}

test_exit()
{
    run_shell $'exit\n'

    assert_rc 0 &&
    assert_stderr_empty &&
    assert_prompt_count 1
}

test_empty_command()
{
    run_shell $'\nexit\n'

    assert_rc 0 &&
    assert_stderr_empty &&
    assert_prompt_count 2
}

test_whitespace_command()
{
    run_shell $'   \t   \nexit\n'

    assert_rc 0 &&
    assert_stderr_empty &&
    assert_prompt_count 2
}

test_external_echo()
{
    run_shell $'echo hello mini shell\nexit\n'

    assert_rc 0 &&
    assert_stdout_contains 'hello mini shell' &&
    assert_stdout_contains 'Command [echo] executed successfully. Return code: 0' &&
    assert_stderr_empty
}

test_spaces_and_tabs()
{
    run_shell $'echo    one\t two\tthree\nexit\n'

    assert_rc 0 &&
    assert_stdout_contains 'one two three' &&
    assert_stderr_empty
}

test_multiple_commands_one_pipe()
{
    run_shell $'echo first\necho second\nexit\n'

    assert_rc 0 &&
    assert_stdout_contains 'first' &&
    assert_stdout_contains 'second' &&
    assert_prompt_count 3 &&
    assert_stderr_empty
}

test_cd_valid()
{
    run_shell $'cd /tmp\npwd\nexit\n'

    assert_rc 0 &&
    assert_stdout_contains '/tmp' &&
    assert_stderr_empty
}

test_cd_missing_argument()
{
    run_shell $'cd\necho shell-still-running\nexit\n'

    assert_rc 0 &&
    assert_stderr_contains 'Usage: cd <directory>' &&
    assert_stdout_contains 'shell-still-running'
}

test_cd_too_many_arguments()
{
    run_shell $'cd /tmp extra\necho shell-still-running\nexit\n'

    assert_rc 0 &&
    assert_stderr_contains 'Usage: cd <directory>' &&
    assert_stdout_contains 'shell-still-running'
}

test_cd_nonexistent()
{
    local missing="${TMP_ROOT}/directory-that-does-not-exist"

    run_shell "cd ${missing}"$'\necho shell-still-running\nexit\n'

    assert_rc 0 &&
    assert_stderr_contains 'cd:' &&
    assert_stdout_contains 'shell-still-running'
}

test_unknown_command()
{
    local command='__bash_mini_command_that_should_not_exist__'

    run_shell "${command}"$'\nexit\n'

    assert_rc 0 &&
    assert_stderr_contains "[${command}]: Unknown Command"
}

test_bin_fallback()
{
    local home_dir="${TMP_ROOT}/empty-home"
    mkdir -p "${home_dir}"

    run_shell $'echo BIN_FALLBACK_OK\nexit\n' env HOME="${home_dir}"

    assert_rc 0 &&
    assert_stdout_contains 'BIN_FALLBACK_OK' &&
    assert_stderr_empty
}

test_home_precedence_and_nonzero_status()
{
    local home_dir="${TMP_ROOT}/home-precedence"
    local helper="${home_dir}/homecmd"

    mkdir -p "${home_dir}"

    cat > "${helper}" <<'EOF'
#!/bin/sh
printf 'HOME_EXECUTABLE_WON\n'
exit 7
EOF
    chmod +x "${helper}"

    run_shell $'homecmd\nexit\n' env HOME="${home_dir}"

    assert_rc 0 &&
    assert_stdout_contains 'HOME_EXECUTABLE_WON' &&
    assert_stdout_contains 'Command [homecmd] finished. Return code: 7' &&
    assert_stderr_empty
}

test_non_executable_home_candidate_falls_back_to_bin()
{
    local home_dir="${TMP_ROOT}/non-executable-home"
    local fake_echo="${home_dir}/echo"

    mkdir -p "${home_dir}"

    cat > "${fake_echo}" <<'EOF'
#!/bin/sh
printf 'THIS_MUST_NOT_RUN\n'
EOF
    chmod -x "${fake_echo}"

    run_shell $'echo REAL_BIN_ECHO\nexit\n' env HOME="${home_dir}"

    assert_rc 0 &&
    assert_stdout_contains 'REAL_BIN_ECHO' &&
    assert_stdout_not_contains 'THIS_MUST_NOT_RUN' &&
    assert_stderr_empty
}

test_argument_vector_reaches_child()
{
    local home_dir="${TMP_ROOT}/argv-home"
    local helper="${home_dir}/argcmd"

    mkdir -p "${home_dir}"

    cat > "${helper}" <<'EOF'
#!/bin/sh
for arg in "$@"; do
    printf '<%s>\n' "$arg"
done
EOF
    chmod +x "${helper}"

    run_shell $'argcmd alpha beta gamma\nexit\n' env HOME="${home_dir}"

    assert_rc 0 &&
    assert_stdout_contains '<alpha>' &&
    assert_stdout_contains '<beta>' &&
    assert_stdout_contains '<gamma>' &&
    assert_stderr_empty
}

test_child_signal_status()
{
    local home_dir="${TMP_ROOT}/signal-home"
    local helper="${home_dir}/signalcmd"

    mkdir -p "${home_dir}"

    cat > "${helper}" <<'EOF'
#!/bin/sh
kill -TERM $$
EOF
    chmod +x "${helper}"

    run_shell $'signalcmd\nexit\n' env HOME="${home_dir}"

    assert_rc 0 &&
    assert_stdout_contains 'Command [signalcmd] terminated by signal: 15' &&
    assert_stderr_empty
}

test_home_unset_uses_bin()
{
    run_shell $'pwd\nexit\n' env -u HOME

    assert_rc 0 &&
    assert_stdout_contains 'Command [pwd] executed successfully. Return code: 0' &&
    assert_stderr_empty
}

test_crlf_input()
{
    run_shell $'echo CRLF_OK\r\nexit\r\n'

    assert_rc 0 &&
    assert_stdout_contains 'CRLF_OK' &&
    assert_stderr_empty
}

test_final_line_without_newline()
{
    run_shell 'pwd'

    assert_rc 0 &&
    assert_stdout_contains 'Command [pwd] executed successfully. Return code: 0' &&
    assert_stderr_empty
}

test_too_many_arguments_and_recovery()
{
    local command='echo'
    local i

    # MAX_ARGS is 128, and one slot must remain NULL.
    # 1 command + 127 arguments = 128 tokens -> rejected.
    for ((i = 0; i < 127; i++)); do
        command+=' x'
    done

    run_shell "${command}"$'\necho ARG_RECOVERY_OK\nexit\n'

    assert_rc 0 &&
    assert_stderr_contains 'Error: too many command arguments (maximum 127).' &&
    assert_stdout_contains 'ARG_RECOVERY_OK'
}

test_oversized_command_and_recovery()
{
    local long_command
    long_command="$(printf 'x%.0s' {1..5000})"

    run_shell "${long_command}"$'\necho LONG_RECOVERY_OK\nexit\n'

    assert_rc 0 &&
    assert_stderr_contains 'Error: command exceeds 4095 bytes.' &&
    assert_stdout_contains 'LONG_RECOVERY_OK'
}

main()
{
    cd "${REPO_ROOT}" || exit 1

    printf '%s\n' "${CYAN}========================================${RESET}"
    printf '%s\n' "${CYAN} MINI BASH AUTOMATED TEST SUITE${RESET}"
    printf '%s\n' "${CYAN}========================================${RESET}"
    printf 'Binary: %s\n\n' "${BIN}"

    if [[ ! -x "${BIN}" ]]; then
        printf '%sERROR:%s %s does not exist or is not executable.\n' \
            "${RED}" "${RESET}" "${BIN}"
        printf 'Build it first with: make check-shell\n'
        exit 1
    fi

    run_test '01. exit terminates cleanly' test_exit
    run_test '02. empty command is ignored' test_empty_command
    run_test '03. whitespace-only command is ignored' test_whitespace_command
    run_test '04. /bin external command executes' test_external_echo
    run_test '05. parser handles repeated spaces and tabs' test_spaces_and_tabs
    run_test '06. buffered reader handles multiple commands' test_multiple_commands_one_pipe
    run_test '07. cd changes parent shell working directory' test_cd_valid
    run_test '08. cd without argument reports usage and continues' test_cd_missing_argument
    run_test '09. cd with extra argument reports usage' test_cd_too_many_arguments
    run_test '10. invalid cd reports system error and continues' test_cd_nonexistent
    run_test '11. unknown command reports required diagnostic' test_unknown_command
    run_test '12. resolver falls back to /bin' test_bin_fallback
    run_test '13. $HOME executable wins and status 7 is decoded' test_home_precedence_and_nonzero_status
    run_test '14. non-executable HOME candidate falls back to /bin' test_non_executable_home_candidate_falls_back_to_bin
    run_test '15. argv arguments reach the external child' test_argument_vector_reaches_child
    run_test '16. child signal termination is decoded' test_child_signal_status
    run_test '17. missing HOME still allows /bin lookup' test_home_unset_uses_bin
    run_test '18. CRLF command input is accepted' test_crlf_input
    run_test '19. final command without newline is accepted' test_final_line_without_newline
    run_test '20. too many arguments are rejected and reader recovers' test_too_many_arguments_and_recovery
    run_test '21. oversized command is discarded and reader recovers' test_oversized_command_and_recovery

    printf '\n%s\n' "${CYAN}========================================${RESET}"
    printf 'Result: %s%d passed%s, %s%d failed%s, %d total\n' \
        "${GREEN}" "${PASSED}" "${RESET}" \
        "${RED}" "${FAILED}" "${RESET}" \
        "${TOTAL}"
    printf '%s\n' "${CYAN}========================================${RESET}"

    if [[ "${FAILED}" -ne 0 ]]; then
        exit 1
    fi

    exit 0
}

main "$@"
