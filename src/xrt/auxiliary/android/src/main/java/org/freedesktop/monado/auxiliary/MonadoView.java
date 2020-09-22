// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Class to inject a custom surface into an activity.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_android_java
 */

package org.freedesktop.monado.auxiliary;

import android.app.Activity;
import android.content.Context;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.WindowManager;

import androidx.annotation.NonNull;
import androidx.annotation.Keep;

@Keep
public class MonadoView extends SurfaceView implements SurfaceHolder.Callback, SurfaceHolder.Callback2 {
    private static final String TAG = "MonadoView";

    @Keep
    public static MonadoView attachToActivity(@NonNull final Activity activity) {
        Log.i(TAG, "Starting to add a new surface!");

        final MonadoView view = new MonadoView(activity);

        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Log.i(TAG, "Starting runOnUiThread");
                WindowManager windowManager = activity.getWindowManager();
                windowManager.addView(view, new WindowManager.LayoutParams());
                view.requestFocus();
                SurfaceHolder surfaceHolder = view.getHolder();
                surfaceHolder.addCallback(view);
                Log.i(TAG, "Registered callbacks!");
            }
        });
        return view;

    }

    public SurfaceHolder currentSurfaceHolder;

    public MonadoView(Context context) {
        super(context);
    }

    public Surface getASurface() {
        SurfaceHolder surfaceHolder = getHolder();
        return surfaceHolder.getSurface();
    }

    public SurfaceHolder getSurfaceHolder() {
        return currentSurfaceHolder;
    }

    @Override
    public void surfaceCreated(@NonNull SurfaceHolder surfaceHolder) {
        currentSurfaceHolder = surfaceHolder;
        Log.i(TAG, "surfaceCreated: Got a surface holder!");
    }

    @Override
    public void surfaceChanged(@NonNull SurfaceHolder surfaceHolder, int format, int width, int height) {
        currentSurfaceHolder = surfaceHolder;
        Log.i(TAG, "surfaceChanged");
    }

    @Override
    public void surfaceDestroyed(@NonNull SurfaceHolder surfaceHolder) {
        //! @todo this function should block until the surface is no longer used in the native code.
        Log.i(TAG, "surfaceDestroyed: Lost our surface.");
        if (surfaceHolder == currentSurfaceHolder) {
            currentSurfaceHolder = null;
        }
    }

    @Override
    public void surfaceRedrawNeeded(@NonNull SurfaceHolder surfaceHolder) {
        currentSurfaceHolder = surfaceHolder;
        Log.i(TAG, "surfaceRedrawNeeded");
    }

}
