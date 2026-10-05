/* SPDX-License-Identifier: GPL-3.0-or-later
 * Standalone synthetic diagnostic: GDI painting, guest-window input messages and bitmap extraction.
 * Contains no game code or game assets. Build with MinGW -mwindows.
 * Run only with the isolated headless configuration in headless_probe_config.json.
 * The output files are completion evidence; Boxedwine exit code alone is not.
 */
#define WIN32_LEAN_AND_MEAN
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
static int mouse_messages = 0, key_messages = 0;
static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_LBUTTONUP) {
        ++mouse_messages;
        return 0;
    }
    if (message == WM_KEYUP) {
        ++key_messages;
        return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        RECT left = {0, 0, 64, 96}, right = {64, 0, 128, 96};
        HBRUSH red = CreateSolidBrush(RGB(255, 0, 0)), blue = CreateSolidBrush(RGB(0, 0, 255));
        FillRect(dc, &left, red);
        FillRect(dc, &right, blue);
        DeleteObject(red);
        DeleteObject(blue);
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcA(window, message, w, l);
}
int main(void) {
    puts("probe: main entered");
    fflush(stdout);
    WNDCLASSA cls = {0};
    cls.lpfnWndProc = proc;
    cls.hInstance = GetModuleHandleA(NULL);
    cls.lpszClassName = "Rich4HeadlessProbe";
    if (!RegisterClassA(&cls)) {
        printf("probe: RegisterClass failed %lu\n", GetLastError());
        fflush(stdout);
        return 2;
    }
    puts("probe: registered");
    fflush(stdout);
    HWND window = CreateWindowExA(0, cls.lpszClassName, "Headless GDI probe", WS_POPUP, 0, 0, 128,
                                  96, NULL, NULL, cls.hInstance, NULL);
    if (!window) {
        printf("probe: CreateWindow failed %lu\n", GetLastError());
        fflush(stdout);
        return 3;
    }
    puts("probe: created");
    fflush(stdout);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    PostMessageA(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(32, 48));
    PostMessageA(window, WM_LBUTTONUP, 0, MAKELPARAM(32, 48));
    PostMessageA(window, WM_KEYDOWN, VK_SPACE, 0);
    PostMessageA(window, WM_KEYUP, VK_SPACE, 0);
    MSG message;
    DWORD until = GetTickCount() + 1200;
    while (GetTickCount() < until) {
        while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
        Sleep(10);
    }
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = 128;
    bmi.bmiHeader.biHeight = -96;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    HDC src = GetDC(window), memory = CreateCompatibleDC(src);
    void* bits = NULL;
    HBITMAP bitmap = CreateDIBSection(src, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ old = SelectObject(memory, bitmap);
    if (!BitBlt(memory, 0, 0, 128, 96, src, 0, 0, SRCCOPY)) {
        return 4;
    }
    GdiFlush();
    uint32_t* colors = bits;
    uint32_t first = colors[48 * 128 + 32] & 0xffffff, last = colors[48 * 128 + 96] & 0xffffff;
    BITMAPFILEHEADER head = {0};
    head.bfType = 0x4d42;
    head.bfOffBits = sizeof(head) + sizeof(BITMAPINFOHEADER);
    head.bfSize = head.bfOffBits + 128 * 96 * 4;
    FILE* out = fopen("gdi-probe.bmp", "wb");
    if (!out) {
        return 5;
    }
    fwrite(&head, 1, sizeof(head), out);
    fwrite(&bmi.bmiHeader, 1, sizeof(BITMAPINFOHEADER), out);
    fwrite(bits, 1, 128 * 96 * 4, out);
    fclose(out);
    out = fopen("result.txt", "wb");
    if (!out) {
        return 6;
    }
    fprintf(out, "screen=%dx%d client=128x96 red=%06lx blue=%06lx mouse=%d key=%d\n",
            GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), (unsigned long)first,
            (unsigned long)last, mouse_messages, key_messages);
    fclose(out);
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(window, src);
    DestroyWindow(window);
    FreeConsole();
    ExitProcess(
        first == 0xff0000 && last == 0x0000ff && mouse_messages == 1 && key_messages == 1 ? 0 : 7);
}
