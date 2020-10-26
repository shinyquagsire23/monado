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
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.WindowManager;

import androidx.annotation.Keep;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import java.util.Calendar;

@Keep
public class MonadoView extends SurfaceView implements SurfaceHolder.Callback, SurfaceHolder.Callback2 {
    private static final String TAG = "MonadoView";
    /// The activity we've connected to.
    private final Activity activity;

    /// Guards currentSurfaceHolder
    private final Object currentSurfaceHolderSync = new Object();
    public int width = -1;
    public int height = -1;
    public int format = -1;

    /// Guarded by currentSurfaceHolderSync
    private SurfaceHolder currentSurfaceHolder = null;

    private MonadoView(Activity activity) {
        super(activity);
        this.activity = activity;
    }

    /**
     * Construct and start attaching a MonadoView to a client application.
     *
     * @param activity The activity to attach to.
     * @return The MonadoView instance created and asynchronously attached.
     */
    @NonNull
    @Keep
    public static MonadoView attachToActivity(@NonNull final Activity activity) {
        Log.i(TAG, "Starting to add a new surface!");

        final MonadoView view = new MonadoView(activity);

        activity.runOnUiThread(() -> {
            Log.i(TAG, "Starting runOnUiThread");
            WindowManager windowManager = activity.getWindowManager();
            windowManager.addView(view, new WindowManager.LayoutParams());
            view.requestFocus();
            SurfaceHolder surfaceHolder = view.getHolder();
            surfaceHolder.addCallback(view);
            Log.i(TAG, "Registered callbacks!");
        });
        return view;

    }

    /**
     * Block up to a specified amount of time, waiting for the surfaceCreated callback to be fired
     * and populate the currentSurfaceHolder.
     * <p>
     * If it returns a SurfaceHolder, the `usedByNativeCode` flag will be set.
     * <p>
     * Called by native code!
     *
     * @param wait_ms Max duration you prefer to wait, in millseconds. Spurious wakeups mean this
     *                not be totally precise.
     * @return A SurfaceHolder or null.
     */
    @Keep
    public @Nullable
    SurfaceHolder waitGetSurfaceHolder(int wait_ms) {
        long currentTime = Calendar.getInstance().getTimeInMillis();
        long timeout = currentTime + wait_ms;
        SurfaceHolder ret = null;
        synchronized (currentSurfaceHolderSync) {
            while (currentSurfaceHolder == null
                    && Calendar.getInstance().getTimeInMillis() < timeout) {
                try {
                    currentSurfaceHolderSync.wait(wait_ms, 0);
                    ret = currentSurfaceHolder;
                } catch (InterruptedException e) {
                    // stop waiting
                    break;
                }
            }
        }
        return ret;
    }

    @Override
    public void surfaceCreated(@NonNull SurfaceHolder surfaceHolder) {
        synchronized (currentSurfaceHolderSync) {
            currentSurfaceHolder = surfaceHolder;
            currentSurfaceHolderSync.notifyAll();
        }
        Log.i(TAG, "surfaceCreated: Got a surface holder!");
    }

    @Override
    public void surfaceChanged(@NonNull SurfaceHolder surfaceHolder, int format, int width, int height) {

        synchronized (currentSurfaceHolderSync) {
            currentSurfaceHolder = surfaceHolder;
            this.format = format;
            this.width = width;
            this.height = height;
            currentSurfaceHolderSync.notifyAll();
        }
        Log.i(TAG, "surfaceChanged");
    }

    @Override
    public void surfaceDestroyed(@NonNull SurfaceHolder surfaceHolder) {
        //! @todo this function should block until the surface is no longer used in the native code.
        Log.i(TAG, "surfaceDestroyed: Lost our surface.");
        synchronized (currentSurfaceHolderSync) {
            if (surfaceHolder == currentSurfaceHolder) {
                currentSurfaceHolder = null;
            }
        }
    }

    @Override
    public void surfaceRedrawNeeded(@NonNull SurfaceHolder surfaceHolder) {
//        currentSurfaceHolder = surfaceHolder;
        Log.i(TAG, "surfaceRedrawNeeded");
    }

}
