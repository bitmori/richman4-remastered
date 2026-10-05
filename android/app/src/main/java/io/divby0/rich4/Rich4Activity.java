// SPDX-License-Identifier: GPL-3.0-or-later
package io.divby0.rich4;

import org.libsdl.app.SDLActivity;

/** Hosts the native game (libmain.so, built from tools/native_game.cpp) through SDL3. */
public class Rich4Activity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "main"};
    }
}
