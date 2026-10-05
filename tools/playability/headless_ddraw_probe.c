/* SPDX-License-Identifier: GPL-3.0-or-later
 * Standalone synthetic diagnostic: DirectDraw 16-bit backbuffer-to-primary drawing and extraction.
 * Contains no game code or game assets. Build with MinGW -mwindows.
 * Run only with the isolated headless configuration in headless_probe_config.json.
 * The output files are completion evidence; Boxedwine exit code alone is not.
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <ddraw.h>
#include <stdint.h>
#include <stdio.h>
#include <windows.h>
static void check(const char* step, HRESULT result) {
    if (FAILED(result)) {
        FILE* f = fopen("ddraw-result.txt", "wb");
        if (f) {
            fprintf(f, "FAIL %s hr=%08lx\n", step, (unsigned long)result);
            fclose(f);
        }
        ExitProcess(2);
    }
}
int main(void) {
    WNDCLASSA c = {0};
    c.lpfnWndProc = DefWindowProcA;
    c.hInstance = GetModuleHandleA(NULL);
    c.lpszClassName = "Rich4HeadlessDDraw";
    RegisterClassA(&c);
    HWND window = CreateWindowExA(0, c.lpszClassName, "Headless DirectDraw probe", WS_POPUP, 0, 0,
                                  640, 480, NULL, NULL, c.hInstance, NULL);
    if (!window) {
        ExitProcess(3);
    }
    ShowWindow(window, SW_SHOW);
    IDirectDraw7* dd = NULL;
    check("DirectDrawCreateEx", DirectDrawCreateEx(NULL, (void**)&dd, &IID_IDirectDraw7, NULL));
    check("SetCooperativeLevel",
          IDirectDraw7_SetCooperativeLevel(dd, window, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN));
    check("SetDisplayMode", IDirectDraw7_SetDisplayMode(dd, 640, 480, 16, 0, 0));
    DDSURFACEDESC2 desc = {0};
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS;
    desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    IDirectDrawSurface7 *primary = NULL, *back = NULL;
    check("CreatePrimary", IDirectDraw7_CreateSurface(dd, &desc, &primary, NULL));
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    desc.dwWidth = 640;
    desc.dwHeight = 480;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    check("CreateBack", IDirectDraw7_CreateSurface(dd, &desc, &back, NULL));
    DDSURFACEDESC2 locked = {0};
    locked.dwSize = sizeof(locked);
    check("LockBack", IDirectDrawSurface7_Lock(back, NULL, &locked, DDLOCK_WAIT, NULL));
    uint32_t red = locked.ddpfPixelFormat.dwRBitMask, blue = locked.ddpfPixelFormat.dwBBitMask,
             bits = locked.ddpfPixelFormat.dwRGBBitCount;
    if (bits != 16) {
        check("UnexpectedBpp", E_FAIL);
    }
    for (int y = 0; y < 480; ++y) {
        uint16_t* line = (uint16_t*)((unsigned char*)locked.lpSurface + y * locked.lPitch);
        for (int x = 0; x < 640; ++x) {
            line[x] = (uint16_t)(x < 320 ? red : blue);
        }
    }
    check("UnlockBack", IDirectDrawSurface7_Unlock(back, NULL));
    check("Blt", IDirectDrawSurface7_Blt(primary, NULL, back, NULL, DDBLT_WAIT, NULL));
    Sleep(300);
    locked.dwSize = sizeof(locked);
    check("LockPrimary",
          IDirectDrawSurface7_Lock(primary, NULL, &locked, DDLOCK_WAIT | DDLOCK_READONLY, NULL));
    unsigned bad = 0;
    FILE* image = fopen("ddraw-probe.ppm", "wb");
    if (image) {
        fprintf(image, "P6\n640 480\n255\n");
    }
    for (int y = 0; y < 480; ++y) {
        uint16_t* line = (uint16_t*)((unsigned char*)locked.lpSurface + y * locked.lPitch);
        for (int x = 0; x < 640; ++x) {
            uint16_t value = line[x];
            bad += value != (x < 320 ? red : blue);
            unsigned char rgb[3] = {(value & red) ? 255 : 0, 0, (value & blue) ? 255 : 0};
            if (image) {
                fwrite(rgb, 1, 3, image);
            }
        }
    }
    if (image) {
        fclose(image);
    }
    check("UnlockPrimary", IDirectDrawSurface7_Unlock(primary, NULL));
    FILE* f = fopen("ddraw-result.txt", "wb");
    if (f) {
        fprintf(f, "PASS bits=%lu red=%04lx blue=%04lx compared=307200 mismatches=%u\n",
                (unsigned long)bits, (unsigned long)red, (unsigned long)blue, bad);
        fclose(f);
    }
    IDirectDrawSurface7_Release(back);
    IDirectDrawSurface7_Release(primary);
    IDirectDraw7_RestoreDisplayMode(dd);
    IDirectDraw7_SetCooperativeLevel(dd, window, DDSCL_NORMAL);
    IDirectDraw7_Release(dd);
    DestroyWindow(window);
    ExitProcess(bad ? 4 : 0);
}
