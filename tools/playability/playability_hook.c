/* SPDX-License-Identifier: GPL-3.0-or-later
 * Observer/input driver for the REAL, initialized game process. Menus, dice,
 * movement and AI execute their original code via the actual window callbacks.
 * Only snapshot/save capture is exposed through a private window message.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define U8(address) (*(volatile uint8_t *)(uintptr_t)(address))
#define U16(address) (*(volatile uint16_t *)(uintptr_t)(address))
#define U32(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define I32(address) (*(volatile int32_t *)(uintptr_t)(address))
#define SNAPSHOT_MESSAGE (WM_APP + 0x4a0)
#define MAIN_CALLBACK 0x40257aU
#define LOAD_CALLBACK 0x40363aU
#define GAME_CALLBACK 0x417e26U

static WNDPROC original_window_proc;
static CRITICAL_SECTION log_lock;
static unsigned requested_turns, completed_turns;
static int observed_roll, observed_movement, observed_other_player;
static char run_id[96];
static struct {
    int player;
    uint16_t before_node, after_node;
    uint32_t before_date, after_date;
    int roll, movement, other_player;
} round_evidence[20];

static uint32_t callback(void) {
    int depth = I32(0x46cad8);
    return depth >= 0 && depth < 20 ? U32(0x48a010 + (uint32_t)depth * 4) : 0;
}

static int human_ready(void) {
    int player = I32(0x49910c);
    return I32(0x46cad8) == 1 && callback() == GAME_CALLBACK && U8(0x46cafd) &&
           player >= 0 && player < 4 && (U8(0x496b68 + player * 0x68 + 21) & 7) == 1 &&
           U8(0x498ea0 + player * 0x34 + 2) == 0;
}

static void event(const char *name, int x, int y) {
    EnterCriticalSection(&log_lock);
    FILE *output = fopen("events.jsonl", "ab");
    if (output) {
        int player = I32(0x49910c);
        fprintf(output, "{\"event\":\"%s\",\"run_id\":\"%s\",\"tick\":%lu,\"callback\":%lu,\"callback_depth\":%ld,\"current_player\":%d,\"input_ready\":%u,\"phase\":%d,\"node\":%u,\"date\":%lu,\"steps_remaining\":%ld,\"dice_total\":%ld,\"x\":%d,\"y\":%d}\n",
                name, run_id, GetTickCount(), (unsigned long)callback(), (long)I32(0x46cad8), player, U8(0x46cafd),
                player >= 0 && player < 4 ? U8(0x498ea0 + player * 0x34 + 2) : -1,
                player >= 0 && player < 4 ? U16(0x496b68 + player * 0x68 + 12) : 0,
                (unsigned long)U32(0x497160), (long)I32(0x48baf8), (long)I32(0x48bafc), x, y);
        fclose(output);
    }
    LeaveCriticalSection(&log_lock);
}

static int dump_state(const char *name) {
    FILE *output = fopen(name, "wb");
    if (!output) return 0;
    fprintf(output, "{\"run_id\":\"%s\",\"scope\":\"original_game_real_process\",\"native_main_loop\":false,\"background_music_setting\":%u,\"date\":%lu,\"current_player\":%ld,\"map_id\":%u,\"stage_id\":%u,\"callback\":%lu,\"input_ready\":%u,\"steps_remaining\":%ld,\"dice_total\":%ld,\"players\":[",
            run_id, U8(0x49715a), (unsigned long)U32(0x497160), (long)I32(0x49910c), U16(0x4991b8), U16(0x4991b6),
            (unsigned long)callback(), U8(0x46cafd), (long)I32(0x48baf8), (long)I32(0x48bafc));
    for (unsigned i = 0; i < 4; ++i) {
        uintptr_t p = 0x496b68 + i * 0x68, r = 0x498ea0 + i * 0x34;
        fprintf(output, "%s{\"index\":%u,\"node\":%u,\"last_node\":%u,\"x\":%u,\"y\":%u,\"cash\":%ld,\"bank\":%ld,\"loan\":%ld,\"controller\":%u,\"phase\":%u,\"flags\":%u}",
                i ? "," : "", i, U16(p + 12), U16(p + 14), U16(p + 8), U16(p + 10),
                (long)I32(p + 28), (long)I32(p + 32), (long)I32(p + 36), U8(p + 21), U8(r + 2), U8(r));
    }
    fputs("]}\n", output);
    int okay = !ferror(output);
    return fclose(output) == 0 && okay;
}

static void dump_frame(HWND window, const char *name) {
    HDC source = GetDC(window), memory = source ? CreateCompatibleDC(source) : NULL;
    if (!source || !memory) { if (source) ReleaseDC(window, source); return; }
    BITMAPINFO info = {0};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 640;
    info.bmiHeader.biHeight = -480;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *pixels = NULL;
    HBITMAP bitmap = CreateDIBSection(source, &info, DIB_RGB_COLORS, &pixels, NULL, 0);
    if (bitmap) {
        HGDIOBJ old = SelectObject(memory, bitmap);
        if (BitBlt(memory, 0, 0, 640, 480, source, 0, 0, SRCCOPY)) {
            BITMAPFILEHEADER header = {0};
            header.bfType = 0x4d42;
            header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
            header.bfSize = header.bfOffBits + 640 * 480 * 4;
            FILE *output = fopen(name, "wb");
            if (output) {
                fwrite(&header, 1, sizeof(header), output);
                fwrite(&info.bmiHeader, 1, sizeof(BITMAPINFOHEADER), output);
                fwrite(pixels, 4, 640 * 480, output);
                fclose(output);
            }
        }
        SelectObject(memory, old);
        DeleteObject(bitmap);
    }
    DeleteDC(memory);
    ReleaseDC(window, source);
}

static LRESULT CALLBACK observing_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == SNAPSHOT_MESSAGE) {
        if (!human_ready()) return 0;
        typedef void (__cdecl *save_fn)(int);
        const int after = wparam != 0;
        const char *save_name = after ? "SAVE9.DAT" : "SAVE8.DAT";
        if (!DeleteFileA(save_name) && GetLastError() != ERROR_FILE_NOT_FOUND) return 0;
        ((save_fn)(uintptr_t)0x402fd1)(after ? 9 : 8);
        if (GetFileAttributesA(save_name) == INVALID_FILE_ATTRIBUTES) return 0;
        if (!CopyFileA(after ? "SAVE9.DAT" : "SAVE8.DAT", after ? "after.DAT" : "before.DAT", FALSE)) return 0;
        if (!dump_state(after ? "after.json" : "before.json")) return 0;
        dump_frame(window, after ? "after.bmp" : "before.bmp");
        event(after ? "snapshot_after" : "snapshot_before", 0, 0);
        return 1;
    }
    return CallWindowProcA(original_window_proc, window, message, wparam, lparam);
}

static int snapshot(HWND window, int after) {
    DWORD_PTR result = 0;
    return SendMessageTimeoutA(window, SNAPSHOT_MESSAGE, after, 0, SMTO_ABORTIFHUNG, 5000, &result) && result == 1;
}

static void click(HWND window, int x, int y, UINT button) {
    PostMessageA(window, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
    PostMessageA(window, button, button == WM_LBUTTONDOWN ? MK_LBUTTON : 0, MAKELPARAM(x, y));
}

static void report(HWND window, int passed, const char *reason) {
    event(passed ? "completed" : "failed", 0, 0);
    if (!passed) {
        dump_state("failure.json");
        if (window) dump_frame(window, "failure.bmp");
    }
    FILE *output = fopen("completion.json", "wb");
    if (output) {
        fprintf(output, "{\"status\":\"%s\",\"reason\":\"%s\",\"run_id\":\"%s\",\"scope\":\"original_game_real_process\",\"native_main_loop\":false,\"steps\":[{\"name\":\"load_save_via_ui\",\"passed\":%s},{\"name\":\"dice_action_started_observed\",\"passed\":%s},{\"name\":\"player_movement_observed\",\"passed\":%s},{\"name\":\"other_player_turn_observed\",\"passed\":%s},{\"name\":\"return_to_human_ready\",\"passed\":%s}],\"facts\":{\"requested_turns\":%u,\"completed_turns\":%u,\"background_music_setting\":%u,\"dice_observation\":\"phase_2_or_1_or_positive_steps_not_visual_detection\",\"save_capture\":\"original_save_function_on_game_thread\",\"input\":\"Win32_messages_to_virtual_game_window\",\"host_desktop_input\":false},\"rounds\":[",
                passed ? "passed" : "failed", reason, run_id,
                GetFileAttributesA("before.DAT") != INVALID_FILE_ATTRIBUTES ? "true" : "false",
                observed_roll ? "true" : "false", observed_movement ? "true" : "false",
                observed_other_player ? "true" : "false", completed_turns >= requested_turns ? "true" : "false",
                requested_turns, completed_turns, U8(0x49715a));
        for (unsigned i = 0; i < completed_turns; ++i) {
            fprintf(output, "%s{\"player\":%d,\"before_node\":%u,\"after_node\":%u,\"before_date\":%lu,\"after_date\":%lu,\"roll_started\":%s,\"movement\":%s,\"other_player\":%s}",
                    i ? "," : "", round_evidence[i].player, round_evidence[i].before_node, round_evidence[i].after_node,
                    (unsigned long)round_evidence[i].before_date, (unsigned long)round_evidence[i].after_date,
                    round_evidence[i].roll ? "true" : "false", round_evidence[i].movement ? "true" : "false",
                    round_evidence[i].other_player ? "true" : "false");
        }
        fputs("]}\n", output);
        fclose(output);
    }
    if (window) PostMessageA(window, WM_CLOSE, 0, 0);
}

static DWORD WINAPI drive_game(void *unused) {
    (void)unused;
    unsigned timeout = 180, slot = 1;
    requested_turns = 1;
    GetEnvironmentVariableA("RICH4_TEST_RUN_ID", run_id, sizeof(run_id));
    for (size_t i = 0; run_id[i]; ++i) {
        unsigned char c = (unsigned char)run_id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            run_id[i] = '_';
    }
    const char *value = getenv("RICH4_TEST_TIMEOUT"); if (value) timeout = (unsigned)atoi(value);
    value = getenv("RICH4_TEST_TURNS"); if (value) requested_turns = (unsigned)atoi(value);
    value = getenv("RICH4_TEST_SLOT"); if (value) slot = (unsigned)atoi(value);
    if ((uintptr_t)GetModuleHandleA(NULL) != 0x400000 ||
        memcmp((void *)(uintptr_t)0x455040, "\x55\x8b\xec\x53\x51\x56\x57", 7)) {
        report(NULL, 0, "unexpected_game_executable"); return 0;
    }
    HWND window = NULL;
    DWORD began = GetTickCount(), last_modal_action = 0, last_activation = 0;
    DWORD unknown_modal_since = 0, last_progress = began, last_heartbeat = began;
    int stall_captured = 0;
    uint32_t unknown_modal = 0;
    int stage = 0, original_player = -1;
    int round_roll = 0, round_movement = 0, round_other_player = 0;
    uint16_t original_node = 0;
    uint32_t original_date = 0, last_callback = 0;
    unsigned ready_streak = 0;
    int last_player = -1, last_phase = -1, last_node = -1, last_ready = -1;
    while (GetTickCount() - began < timeout * 1000) {
        if (!window) {
            window = (HWND)(uintptr_t)U32(0x48a0d4);
            if (window && IsWindow(window)) {
                original_window_proc = (WNDPROC)(uintptr_t)GetWindowLongA(window, GWL_WNDPROC);
                SetLastError(0);
                if (!SetWindowLongA(window, GWL_WNDPROC, (LONG)(uintptr_t)observing_window_proc) && GetLastError()) {
                    report(window, 0, "cannot_subclass_game_window"); return 0;
                }
                PostMessageA(window, WM_ACTIVATEAPP, TRUE, 0);
                event("window_observed", 0, 0);
            } else { window = NULL; Sleep(50); continue; }
        }
        uint32_t current_callback = callback();
        if (!U8(0x46cb01) && GetTickCount() - last_activation > 1000) {
            PostMessageA(window, WM_ACTIVATEAPP, TRUE, 0);
            event("activate_virtual_game_window", 0, 0);
            last_activation = GetTickCount();
        }
        if (current_callback != last_callback) {
            event("callback_changed", 0, 0); last_callback = current_callback;
            last_progress = GetTickCount(); stall_captured = 0;
        }
        if (current_callback == GAME_CALLBACK) {
            int player = I32(0x49910c);
            int phase = player >= 0 && player < 4 ? U8(0x498ea0 + player * 0x34 + 2) : -1;
            int node = player >= 0 && player < 4 ? U16(0x496b68 + player * 0x68 + 12) : -1;
            int ready = U8(0x46cafd);
            if (player != last_player || phase != last_phase || node != last_node || ready != last_ready) {
                event("turn_state_changed", 0, 0);
                last_player = player; last_phase = phase; last_node = node; last_ready = ready;
                last_progress = GetTickCount(); stall_captured = 0;
            }
        }
        if (stage == 0 && current_callback == MAIN_CALLBACK) {
            uintptr_t resource = U32(0x48a180);
            if (resource) {
                uintptr_t graphic = resource + 12 + 4 * 12;
                int x = 328 - (int16_t)U16(graphic + 4) + (int16_t)U16(graphic) / 2;
                int y = 380 - (int16_t)U16(graphic + 6) + (int16_t)U16(graphic + 2) / 2;
                PostMessageA(window, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
                Sleep(50);
                if (I32(0x48a184) == 1) { click(window, x, y, WM_LBUTTONDOWN); event("click_load_menu", x, y); stage = 1; }
            }
        } else if (stage == 1 && current_callback == LOAD_CALLBACK) {
            int y = slot == 0 ? 57 : 60 + (int)slot * 72;
            click(window, 300, y, WM_LBUTTONDOWN);
            event("click_load_slot", 300, y);
            stage = 2;
        } else if ((stage == 2 || stage == 4) && human_ready()) {
            if (stage == 2 && !snapshot(window, 0)) { report(window, 0, "before_snapshot_failed"); return 0; }
            original_player = I32(0x49910c);
            original_node = U16(0x496b68 + original_player * 0x68 + 12);
            original_date = U32(0x497160);
            uintptr_t bitmap = U32(0x48be08);
            int width = I32(0x48bdd8), height = I32(0x48bddc), found = 0;
            if (bitmap && width > 2 && width < 640 && height > 2 && height < 480) {
                for (int y = 1; y < height - 1 && !found; ++y) {
                    for (int x = 1; x < width - 1; ++x) {
                        uintptr_t pixel = bitmap + y * width + x;
                        if (U8(pixel) == 3 && U8(pixel - 1) == 3 && U8(pixel + 1) == 3 && U8(pixel - width) == 3 && U8(pixel + width) == 3) {
                            int window_x = I32(0x475284) + x, window_y = I32(0x475288) + y;
                            click(window, window_x, window_y, WM_LBUTTONDOWN);
                            event("click_dice", window_x, window_y);
                            found = 1; stage = 3; ready_streak = 0;
                            round_roll = round_movement = round_other_player = 0;
                            break;
                        }
                    }
                }
            }
            if (!found) { report(window, 0, "dice_hit_region_not_found"); return 0; }
        } else if (stage == 3) {
            int player = I32(0x49910c);
            if (player >= 0 && player < 4) {
                int phase = U8(0x498ea0 + player * 0x34 + 2);
                if (phase == 2 || phase == 1 || I32(0x48baf8) > 0) {
                    if (!round_roll) event("dice_action_started_observed", 0, 0);
                    observed_roll = round_roll = 1;
                }
            }
            if (U16(0x496b68 + original_player * 0x68 + 12) != original_node) observed_movement = round_movement = 1;
            if (player >= 0 && player < 4 && player != original_player)
                observed_other_player = round_other_player = 1;
            if (current_callback != GAME_CALLBACK && GetTickCount() - last_modal_action > 1000) {
                if (current_callback == 0x45367e || current_callback == 0x435062 ||
                    current_callback == 0x436ef8 || current_callback == 0x436034 ||
                    current_callback == 0x42f7fc) {
                    click(window, 300, 240, WM_RBUTTONUP);
                    event("dismiss_known_modal_via_right_click", 300, 240);
                    last_modal_action = GetTickCount();
                    unknown_modal_since = 0;
                } else {
                    if (!unknown_modal_since || unknown_modal != current_callback) {
                        unknown_modal = current_callback;
                        unknown_modal_since = GetTickCount();
                        event("unhandled_modal_observed", 0, 0);
                    } else if (GetTickCount() - unknown_modal_since > 15000) {
                        report(window, 0, "unsupported_modal_callback"); return 0;
                    }
                }
            }
            if (current_callback == GAME_CALLBACK) unknown_modal_since = 0;
            if (round_roll && round_movement && round_other_player && human_ready() &&
                player == original_player && U32(0x497160) != original_date) {
                if (++ready_streak >= 3) {
                    round_evidence[completed_turns].player = original_player;
                    round_evidence[completed_turns].before_node = original_node;
                    round_evidence[completed_turns].after_node = U16(0x496b68 + original_player * 0x68 + 12);
                    round_evidence[completed_turns].before_date = original_date;
                    round_evidence[completed_turns].after_date = U32(0x497160);
                    round_evidence[completed_turns].roll = round_roll;
                    round_evidence[completed_turns].movement = round_movement;
                    round_evidence[completed_turns].other_player = round_other_player;
                    ++completed_turns;
                    event("full_round_completed", 0, 0);
                    if (completed_turns >= requested_turns) {
                        if (!snapshot(window, 1)) { report(window, 0, "after_snapshot_failed"); return 0; }
                        report(window, 1, "observed_original_game_round"); return 0;
                    }
                    stage = 4;
                }
            } else ready_streak = 0;
        }
        if (GetTickCount() - last_heartbeat >= 5000) {
            event("observer_heartbeat", 0, 0);
            last_heartbeat = GetTickCount();
        }
        if (stage == 3 && GetTickCount() - last_progress >= 15000 && !stall_captured) {
            /* Diagnostics only: no input, state patch, or synthetic completion. */
            event("game_state_stalled", 0, 0);
            dump_state("failure.json");
            dump_frame(window, "failure.bmp");
            stall_captured = 1;
        }
        if (stage == 3 && GetTickCount() - last_progress >= 45000) {
            report(window, 0, "no_game_state_progress_for_45_seconds"); return 0;
        }
        Sleep(50);
    }
    report(window, 0, "timed_out_before_required_state_transitions");
    return 0;
}

/* The thread-specific WH_GETMESSAGE callback is only a loader rendezvous.
 * Observations and actions still use the game's actual window messages. */
__declspec(dllexport) LRESULT CALLBACK PlayabilityHook(int code, WPARAM wparam, LPARAM lparam) {
    return CallNextHookEx(NULL, code, wparam, lparam);
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        /* The launcher also loads this DLL to obtain the hook export. Only
         * start the driver in the verified original game's process. */
        uintptr_t image = (uintptr_t)GetModuleHandleA(NULL);
        IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
        IMAGE_NT_HEADERS *pe = (IMAGE_NT_HEADERS *)(image + dos->e_lfanew);
        if (image != 0x400000 || pe->OptionalHeader.SizeOfImage < 0x55047 ||
            memcmp((void *)(image + 0x55040), "\x55\x8b\xec\x53\x51\x56\x57", 7)) return TRUE;
        DisableThreadLibraryCalls(module);
        InitializeCriticalSection(&log_lock);
        HANDLE thread = CreateThread(NULL, 0, drive_game, NULL, 0, NULL);
        if (!thread) return FALSE;
        CloseHandle(thread);
    }
    return TRUE;
}
