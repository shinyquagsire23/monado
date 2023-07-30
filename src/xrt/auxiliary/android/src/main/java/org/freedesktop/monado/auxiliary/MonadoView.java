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
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.WindowManager;

import androidx.annotation.GuardedBy;
import androidx.annotation.Keep;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

@Keep
public class MonadoView extends SurfaceView
        implements SurfaceHolder.Callback, SurfaceHolder.Callback2 {
    private static final String TAG = "MonadoView";

    private final Object currentSurfaceHolderSync = new Object();

    public int width = -1;
    public int height = -1;
    public int format = -1;

    private NativeCounterpart nativeCounterpart;

    @GuardedBy("currentSurfaceHolderSync") @Nullable private SurfaceHolder currentSurfaceHolder = null;

    private SystemUiController systemUiController = null;

    public MonadoView(Context context) {
        super(context);

        if (context instanceof Activity) {
            Activity activity = (Activity) context;
            systemUiController = new SystemUiController(activity.getWindow().getDecorView());
            systemUiController.hide();
        }
        SurfaceHolder surfaceHolder = getHolder();
        surfaceHolder.addCallback(this);
    }

    private MonadoView(Context context, long nativePointer) {
        this(context);

        nativeCounterpart = new NativeCounterpart(nativePointer);
    }

    /**
     * Construct and start attaching a MonadoView to a client application.
     *
     * @param activity The activity to attach to.
     * @return The MonadoView instance created and asynchronously attached.
     */
    @NonNull @Keep
    public static MonadoView attachToActivity(@NonNull final Activity activity) {
        final MonadoView view = new MonadoView(activity);
        WindowManager.LayoutParams lp = new WindowManager.LayoutParams();
        lp.flags =
                WindowManager.LayoutParams.FLAG_FULLSCREEN
                        | WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE;
        attachToWindow(activity, view, lp);
        return view;
    }

    /**
     * Construct and start attaching a MonadoView to window.
     *
     * @param displayContext Display context used for looking for target window.
     * @param nativePointer The native android_custom_surface pointer, cast to a long.
     * @param lp Layout parameters associated with view.
     * @return The MonadoView instance created and asynchronously attached.
     */
    @NonNull @Keep
    public static MonadoView attachToWindow(
            @NonNull final Context displayContext,
            long nativePointer,
            WindowManager.LayoutParams lp)
            throws IllegalArgumentException {
        final MonadoView view = new MonadoView(displayContext, nativePointer);
        attachToWindow(displayContext, view, lp);
        return view;
    }

    private static void attachToWindow(
            @NonNull final Context context,
            @NonNull MonadoView view,
            @NonNull WindowManager.LayoutParams lp) {
        Handler handler = new Handler(Looper.getMainLooper());
        handler.post(
                () -> {
                    Log.d(TAG, "Start adding view to window");
                    WindowManager wm =
                            (WindowManager) context.getSystemService(Context.WINDOW_SERVICE);
                    wm.addView(view, lp);

                    SystemUiController systemUiController = new SystemUiController(view);
                    systemUiController.hide();
                });
    }

    /**
     * Remove given MonadoView from window.
     *
     * @param view The view to remove.
     */
    @Keep
    public static void removeFromWindow(@NonNull MonadoView view) {
        Handler handler = new Handler(Looper.getMainLooper());
        handler.post(
                () -> {
                    Log.d(TAG, "Start removing view from window");
                    WindowManager wm =
                            (WindowManager)
                                    view.getContext().getSystemService(Context.WINDOW_SERVICE);
                    wm.removeView(view);
                });
    }

    @NonNull @Keep
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

    /**
     * Block up to a specified amount of time, waiting for the surfaceCreated callback to be fired
     * and populate the currentSurfaceHolder.
     *
     * <p>If it returns a SurfaceHolder, the `usedByNativeCode` flag will be set.
     *
     * <p>Called by native code!
     *
     * @param wait_ms Max duration you prefer to wait, in milliseconds. Spurious wakeups mean this
     *     not be totally precise.
     * @return A SurfaceHolder or null.
     */
    @Keep
    public @Nullable SurfaceHolder waitGetSurfaceHolder(int wait_ms) {
        long currentTime = SystemClock.uptimeMillis();
        long timeout = currentTime + wait_ms;
        SurfaceHolder ret = null;
        synchronized (currentSurfaceHolderSync) {
            ret = currentSurfaceHolder;
            while (currentSurfaceHolder == null && SystemClock.uptimeMillis() < timeout) {
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
            if (nativeCounterpart != null) nativeCounterpart.markAsUsedByNativeCode();
        }
        return ret;
    }

    /**
     * Change the flag and notify those waiting on it, to indicate that native code is done with
     * this object.
     *
     * <p>Called by native code!
     */
    @Keep
    public void markAsDiscardedByNative() {
        if (nativeCounterpart != null) nativeCounterpart.markAsDiscardedByNative(TAG);
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
    public void surfaceChanged(
            @NonNull SurfaceHolder surfaceHolder, int format, int width, int height) {

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
            // ! @todo this function should notify native code that the surface is gone.
            if (nativeCounterpart != null && !nativeCounterpart.blockUntilNativeDiscard(TAG)) {
                Log.i(
                        TAG,
                        "Interrupted in surfaceDestroyed while waiting for native code to finish"
                                + " up.");
            }
        }
    }

    @Override
    public void surfaceRedrawNeeded(@NonNull SurfaceHolder surfaceHolder) {
        //        currentSurfaceHolder = surfaceHolder;
        Log.i(TAG, "surfaceRedrawNeeded");
    }
}
