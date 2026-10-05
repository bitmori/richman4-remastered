// SPDX-License-Identifier: GPL-3.0-or-later
package io.divby0.rich4;

import android.view.HapticFeedbackConstants;

import org.libsdl.app.SDLActivity;

/** Hosts the native game (libmain.so, built from tools/native_game.cpp) through SDL3. */
public class Rich4Activity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "main"};
    }

    /**
     * The title screen's EXIT returns from the native main, and SDL then finishes this activity. SDL
     * refuses to run main again in the same process (it calls System.exit on the next onCreate, so a
     * relaunch would look like a dead tap); end the process now so the next launch starts fresh.
     */
    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (mSDLMainFinished)
            System.exit(0);
    }

    /** Called from native code (rich4::platform::haptic_tap) for every button press. */
    public void hapticTap() {
        runOnUiThread(() -> getWindow().getDecorView().performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP));
    }
}
