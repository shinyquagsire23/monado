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
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.WindowManager;

import androidx.annotation.GuardedBy;
import androidx.annotation.Keep;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import java.util.Calendar;

@Keep
public class MonadoView extends SurfaceView implements SurfaceHolder.Callback, SurfaceHolder.Callback2 {
    private static final String TAG = "MonadoView";

    @NonNull
    private final Context context;

    /// The activity we've connected to.
    @Nullable
    private final Activity activity;
    private final Object currentSurfaceHolderSync = new Object();

    public int width = -1;
    public int height = -1;
    public int format = -1;

    private NativeCounterpart nativeCounterpart;

    @GuardedBy("currentSurfaceHolderSync")
    @Nullable
    private SurfaceHolder currentSurfaceHolder = null;

    public MonadoView(Context context) {
        super(context);
        this.context = context;
        Activity activity;
        if (context instanceof Activity) {
            activity = (Activity) context;
        } else {
            activity = null;
        }
        this.activity = activity;
    }

    public MonadoView(Activity activity) {
        super(activity);
        this.context = activity;
        this.activity = activity;
    }

    private MonadoView(Activity activity, long nativePointer) {
        this(activity);
        nativeCounterpart = new NativeCounterpart(nativePointer);
    }

    /**
     * Construct and start attaching a MonadoView to a client application.
     *
     * @param activity      The activity to attach to.
     * @param nativePointer The native android_custom_surface pointer, cast to a long.
     * @return The MonadoView instance created and asynchronously attached.
     */
    @NonNull
    @Keep
    @SuppressWarnings("deprecation")
    public static MonadoView attachToActivity(@NonNull final Activity activity, long nativePointer) {
        final MonadoView view = new MonadoView(activity, nativePointer);
        view.createSurfaceInActivity();
        return view;
    }

    @NonNull
    @Keep
    public static MonadoView attachToActivity(@NonNull final Activity activity) {
        final MonadoView view = new MonadoView(activity);
        view.createSurfaceInActivity();
        return view;
    }

    @NonNull
    @Keep
    public static DisplayMetrics getDisplayMetrics(@NonNull Context context) {
        DisplayMetrics displayMetrics = new DisplayMetrics();
        WindowManager wm = (WindowManager) context.getSystemService(Context.WINDOW_SERVICE);
        wm.getDefaultDisplay().getMetrics(displayMetrics);
        return displayMetrics;
    }

    @Keep
    public static float getDisplayRefreshRate(@NonNull Context context) {
        WindowManager wm = (WindowManager) context.getSystemService(Context.WINDOW_SERVICE);
        return wm.getDefaultDisplay().getRefreshRate();
    }

    @Keep
    public long getNativePointer() {
        if (nativeCounterpart == null) {
            return 0;
        }
        return nativeCounterpart.getNativePointer();
    }

    private void createSurfaceInActivity() {
        createSurfaceInActivity(false);
    }

    /**
     * @param focusable Indicates MonadoView should be focusable or not
     */
    private void createSurfaceInActivity(boolean focusable) {
        Log.i(TAG, "Starting to add a new surface!");
        activity.runOnUiThread(() -> {
            Log.i(TAG, "Starting runOnUiThread");
            activity.getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

            WindowManager windowManager = activity.getWindowManager();
            WindowManager.LayoutParams lp = new WindowManager.LayoutParams();
            if (focusable) {
                lp.flags = WindowManager.LayoutParams.FLAG_FULLSCREEN;
            } else {
                // There are 2 problems if view is focusable on all-in-one device:
                // 1. Navigation bar won't go away because view gets focus.
                // 2. Underlying activity lost focus and can not receive input.
                lp.flags = WindowManager.LayoutParams.FLAG_FULLSCREEN |
                        WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                        WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE;
            }
            windowManager.addView(this, lp);
            if (focusable) {
                requestFocus();
            }
            SurfaceHolder surfaceHolder = getHolder();
            surfaceHolder.addCallback(this);
            Log.i(TAG, "Registered callbacks!");
        });
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
        if (ret != null) {
            if (nativeCounterpart != null)
                nativeCounterpart.markAsUsedByNativeCode();
        }
        return ret;
    }

    /**
     * Change the flag and notify those waiting on it, to indicate that native code is done with
     * this object.
     * <p>
     * Called by native code!
     */
    @Keep
    public void markAsDiscardedByNative() {
        if (nativeCounterpart != null)
            nativeCounterpart.markAsDiscardedByNative(TAG);
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
        Log.i(TAG, "surfaceDestroyed: Lost our surface.");
        boolean lost = false;
        synchronized (currentSurfaceHolderSync) {
            if (surfaceHolder == currentSurfaceHolder) {
                currentSurfaceHolder = null;
                lost = true;
            }
        }
        if (lost) {
            //! @todo this function should notify native code that the surface is gone.
            if (nativeCounterpart != null && !nativeCounterpart.blockUntilNativeDiscard(TAG)) {
                Log.i(TAG,
                        "Interrupted in surfaceDestroyed while waiting for native code to finish up.");
            }
        }
    }

    @Override
    public void surfaceRedrawNeeded(@NonNull SurfaceHolder surfaceHolder) {
//        currentSurfaceHolder = surfaceHolder;
        Log.i(TAG, "surfaceRedrawNeeded");
    }

}
