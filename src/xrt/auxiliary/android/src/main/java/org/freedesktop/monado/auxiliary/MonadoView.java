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
import android.os.Build;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;

import androidx.annotation.Keep;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.RequiresApi;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.Calendar;

@Keep
public class MonadoView extends SurfaceView implements SurfaceHolder.Callback, SurfaceHolder.Callback2 {
    private static final String TAG = "MonadoView";
    @SuppressWarnings("deprecation")
    private static final int sysUiVisFlags = 0
            // Give us a stable view of content insets
            | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            // Be able to do fullscreen and hide navigation
            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_FULLSCREEN
            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            // we want sticky immersive
            | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY;
    /// The activity we've connected to.
    private final Activity activity;

    /// Guards currentSurfaceHolder
    private final Object currentSurfaceHolderSync = new Object();
    private final Method viewSetSysUiVis;
    private NativeCounterpart nativeCounterpart;

    public int width = -1;
    public int height = -1;
    public int format = -1;

    /// Guarded by currentSurfaceHolderSync
    private SurfaceHolder currentSurfaceHolder = null;

    public MonadoView(Activity activity) {
        super(activity);
        this.activity = activity;
        Method method;
        try {
            method = activity.getWindow().getDecorView().getClass().getMethod("setSystemUiVisibility", int.class);
        } catch (NoSuchMethodException e) {
            // ok
            method = null;
        }
        viewSetSysUiVis = method;
    }

    private MonadoView(Activity activity, long nativePointer) {
        this(activity);
        nativeCounterpart = new NativeCounterpart(nativePointer);
    }

    private void createSurface() {
        Log.i(TAG, "Starting to add a new surface!");
        activity.runOnUiThread(() -> {
            Log.i(TAG, "Starting runOnUiThread");
            activity.getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

            WindowManager windowManager = activity.getWindowManager();
            windowManager.addView(this, new WindowManager.LayoutParams(WindowManager.LayoutParams.FLAG_FULLSCREEN));

            requestFocus();
            SurfaceHolder surfaceHolder = getHolder();
            surfaceHolder.addCallback(this);
            Log.i(TAG, "Registered callbacks!");
        });
    }

    /**
     * Construct and start attaching a MonadoView to a client application.
     *
     * @param activity The activity to attach to.
     * @return The MonadoView instance created and asynchronously attached.
     */
    @NonNull
    @Keep
    @SuppressWarnings("deprecation")
    public static MonadoView attachToActivity(@NonNull final Activity activity, long nativePointer) {
        final MonadoView view = new MonadoView(activity, nativePointer);
        view.createSurface();
        return view;
    }

    @NonNull
    @Keep
    public static MonadoView attachToActivity(@NonNull final Activity activity) {
        final MonadoView view = new MonadoView(activity);
        view.createSurface();
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

    private boolean makeFullscreen() {
        if (activity == null) {
            return false;
        }
        if (viewSetSysUiVis == null) {
            return false;
        }
        View decorView = activity.getWindow().getDecorView();
        //! @todo implement with WindowInsetsController to ward off the stink of deprecation
        try {
            viewSetSysUiVis.invoke(decorView, sysUiVisFlags);
        } catch (IllegalAccessException e) {
            return false;
        } catch (InvocationTargetException e) {
            return false;
        }
        return true;
    }


    /**
     * Add a listener so that if our system UI display state doesn't include all we want, we re-apply.
     */
    @RequiresApi(api = Build.VERSION_CODES.HONEYCOMB)
    @SuppressWarnings("deprecation")
    private void setSystemUiVisChangeListener() {
        activity.getWindow().getDecorView().setOnSystemUiVisibilityChangeListener(visibility -> {
            // If not fullscreen, fix it.
            if (0 == (visibility & View.SYSTEM_UI_FLAG_FULLSCREEN)) {
                makeFullscreen();
            }
        });

    }

    @Override
    public void surfaceCreated(@NonNull SurfaceHolder surfaceHolder) {
        synchronized (currentSurfaceHolderSync) {
            currentSurfaceHolder = surfaceHolder;
            currentSurfaceHolderSync.notifyAll();
        }
        Log.i(TAG, "surfaceCreated: Got a surface holder!");

        if (makeFullscreen()) {
            // If we could make it full screen, make it really stick.
            setSystemUiVisChangeListener();
        }
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

    @NonNull
    @Keep
    public static DisplayMetrics getDisplayMetrics(Activity activity) {
        DisplayMetrics displayMetrics = new DisplayMetrics();
        activity.getWindowManager().getDefaultDisplay().getMetrics(displayMetrics);
        return displayMetrics;
    }

}
