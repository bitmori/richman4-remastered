/* SPDX-License-Identifier: GPL-3.0-or-later
 * Launch only the private test copy of Rich4, then inject its test observer.
 * All input remains inside Wine; no host desktop automation is used.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char run_id[96];

static void failure(const char *operation, DWORD error) {
    FILE *output = fopen("launcher.json", "wb");
    if (output) {
        fprintf(output, "{\"status\":\"failed\",\"run_id\":\"%s\",\"operation\":\"%s\",\"win32_error\":%lu}\n", run_id, operation, error);
        fclose(output);
    }
    fprintf(stderr, "%s: Win32 error %lu\n", operation, error);
}

static int completion_passed(void) {
    FILE *input = fopen("completion.json", "rb");
    if (!input) return 0;
    char text[4096] = {0};
    fread(text, 1, sizeof(text) - 1, input);
    fclose(input);
    return strstr(text, "\"status\":\"passed\"") != NULL;
}

static void record_game_exit(DWORD code, int forced) {
    FILE *input = fopen("completion.json", "rb");
    if (!input) return;
    char text[8192] = {0};
    size_t length = fread(text, 1, sizeof(text) - 1, input);
    fclose(input);
    while (length && (text[length - 1] == '\n' || text[length - 1] == '\r' || text[length - 1] == ' ')) --length;
    if (!length || text[length - 1] != '}') return;
    if (code != 0) {
        char *status = strstr(text, "\"status\":\"passed\"");
        if (status) memcpy(status + strlen("\"status\":\""), "failed", 6);
    }
    text[--length] = 0;
    FILE *output = fopen("completion.json", "wb");
    if (output) {
        fprintf(output, "%s,\"game_exit\":%lu,\"terminated_after_capture\":%s,\"clean_shutdown\":%s}\n", text, code,
                forced ? "true" : "false", !forced && code == 0 ? "true" : "false");
        fclose(output);
    }
}

int main(int argc, char **argv) {
    unsigned timeout = 180, turns = 1, slot = 1;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) { failure("missing argument", ERROR_INVALID_PARAMETER); return 2; }
        unsigned value = (unsigned)strtoul(argv[i + 1], NULL, 10);
        if (!strcmp(argv[i], "--timeout")) timeout = value;
        else if (!strcmp(argv[i], "--turns")) turns = value;
        else if (!strcmp(argv[i], "--slot")) slot = value;
        else { failure("unknown argument", ERROR_INVALID_PARAMETER); return 2; }
    }
    if (!timeout || timeout > 600 || !turns || turns > 20 || slot > 5) {
        failure("argument range", ERROR_INVALID_PARAMETER); return 2;
    }
    const char *stale[] = {"completion.json", "launcher.json", "events.jsonl", "before.DAT", "after.DAT",
                          "before.json", "after.json", "before.bmp", "after.bmp", "failure.json", "failure.bmp"};
    for (size_t i = 0; i < sizeof(stale) / sizeof(stale[0]); ++i) {
        if (!DeleteFileA(stale[i]) && GetLastError() != ERROR_FILE_NOT_FOUND) {
            failure("remove stale test output", GetLastError()); return 2;
        }
    }
    char number[32], dll_path[MAX_PATH];
    if (!GetEnvironmentVariableA("RICH4_TEST_RUN_ID", number, sizeof(number))) {
        snprintf(number, sizeof(number), "%08lx-%08lx", GetCurrentProcessId(), GetTickCount());
        SetEnvironmentVariableA("RICH4_TEST_RUN_ID", number);
    }
    GetEnvironmentVariableA("RICH4_TEST_RUN_ID", run_id, sizeof(run_id));
    snprintf(number, sizeof(number), "%u", timeout);
    SetEnvironmentVariableA("RICH4_TEST_TIMEOUT", number);
    snprintf(number, sizeof(number), "%u", turns);
    SetEnvironmentVariableA("RICH4_TEST_TURNS", number);
    snprintf(number, sizeof(number), "%u", slot);
    SetEnvironmentVariableA("RICH4_TEST_SLOT", number);
    if (!GetFullPathNameA("playability_hook.dll", MAX_PATH, dll_path, NULL) ||
        GetFileAttributesA(dll_path) == INVALID_FILE_ATTRIBUTES) {
        failure("locate hook DLL", GetLastError()); return 2;
    }
    STARTUPINFOA startup = {0};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {0};
    char command[] = "rich4.exe";
    if (!CreateProcessA("rich4.exe", command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) {
        failure("CreateProcess rich4.exe", GetLastError()); return 2;
    }
    /* Wine's message-hook loader performs the DLL load inside the target.
     * Unlike remote memory injection, this also works when the emulator does
     * not implement process_vm_writev/ptrace for WriteProcessMemory. The hook
     * targets only the process/thread created immediately above. */
    HMODULE observer = LoadLibraryA(dll_path);
    if (!observer) {
        failure("load observer in launcher", GetLastError()); TerminateProcess(process.hProcess, 2); return 2;
    }
    union { FARPROC generic; HOOKPROC hook; } entry;
    entry.generic = GetProcAddress(observer, "PlayabilityHook@12");
    if (!entry.generic) entry.generic = GetProcAddress(observer, "PlayabilityHook");
    HHOOK injection = entry.generic ? SetWindowsHookExA(WH_GETMESSAGE, entry.hook, observer, process.dwThreadId) : NULL;
    if (!injection) {
        failure("install child message observer", GetLastError()); TerminateProcess(process.hProcess, 2); return 2;
    }
    CloseHandle(process.hThread);
    DWORD began = GetTickCount(), report_seen = 0;
    int forced = 0;
    while (WaitForSingleObject(process.hProcess, 100) == WAIT_TIMEOUT) {
        DWORD elapsed = GetTickCount() - began;
        if (GetFileAttributesA("completion.json") != INVALID_FILE_ATTRIBUTES) {
            if (!report_seen) report_seen = GetTickCount();
            if (GetTickCount() - report_seen > 3000) {
                /* Only this helper's own private child is terminated. */
                TerminateProcess(process.hProcess, completion_passed() ? 0 : 2);
                forced = 1;
            }
        } else if (elapsed > (timeout + 20) * 1000) {
            failure("gameplay timeout before report", WAIT_TIMEOUT);
            TerminateProcess(process.hProcess, 2);
            forced = 1;
        }
    }
    DWORD exit_code = 0;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hProcess);
    UnhookWindowsHookEx(injection);
    FreeLibrary(observer);
    record_game_exit(exit_code, forced);
    int passed = completion_passed() && exit_code == 0;
    FILE *output = fopen("launcher.json", "wb");
    if (output) {
        fprintf(output, "{\"status\":\"%s\",\"run_id\":\"%s\",\"game_exit\":%lu,\"terminated_after_capture\":%s,\"clean_shutdown\":%s}\n",
                passed ? "passed" : "failed", run_id, exit_code, forced ? "true" : "false",
                !forced && exit_code == 0 ? "true" : "false");
        fclose(output);
    }
    return passed ? 0 : 2;
}
