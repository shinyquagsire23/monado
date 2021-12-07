// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Implementation of the Monado AIDL server
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup ipc_android
 */


package org.freedesktop.monado.ipc;

import android.os.ParcelFileDescriptor;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceHolder;

import androidx.annotation.Keep;
import androidx.annotation.NonNull;

import org.jetbrains.annotations.NotNull;

import java.io.IOException;

/**
 * Java implementation of the IMonado IPC interface.
 * <p>
 * (This is the server-side code.)
 * <p>
 * All this does is delegate calls to native JNI implementations.
 * The warning suppression is because Android Studio tends to have a hard time finding
 * the (very real) implementations of these in service-lib.
 */
@Keep
public class MonadoImpl extends IMonado.Stub {

    private static final String TAG = "MonadoImpl";

    static {
        // Load the shared library with the native parts of this class
        // This is the service-lib target.
        System.loadLibrary("monado-service");
    }

    private SurfaceManager surfaceManager;

    public MonadoImpl(@NonNull SurfaceManager surfaceManager) {
        this.surfaceManager = surfaceManager;
        this.surfaceManager.setCallback(new SurfaceHolder.Callback() {
            @Override
            public void surfaceCreated(@NonNull SurfaceHolder holder) {
                Log.i(TAG, "surfaceCreated");
                passAppSurface(holder.getSurface());
            }

            @Override
            public void surfaceChanged(@NonNull SurfaceHolder holder, int format, int width, int height) {
            }

            @Override
            public void surfaceDestroyed(@NonNull SurfaceHolder holder) {
            }
        });
    }

    @Override
    public void connect(@NotNull ParcelFileDescriptor parcelFileDescriptor) {
        int fd = parcelFileDescriptor.getFd();
        Log.i(TAG, "connect: given fd " + fd);
        if (nativeAddClient(fd) != 0) {
            Log.e(TAG, "Failed to transfer client fd ownership!");
            try {
                parcelFileDescriptor.close();
            } catch (IOException e) {
                // do nothing, probably already closed.
            }
        } else {
            Log.i(TAG, "connect: fd ownership transferred");
            parcelFileDescriptor.detachFd();
        }
    }

    @Override
    public void passAppSurface(Surface surface) {
        Log.i(TAG, "passAppSurface");
        if (surface == null) {
            Log.e(TAG, "Received a null Surface from the client!");
            return;
        }
        nativeAppSurface(surface);
        nativeStartServer();
    }

    @Override
    public boolean createSurface(int displayId, boolean focusable) {
        Log.i(TAG, "createSurface");
        return surfaceManager.createSurfaceOnDisplay(displayId, focusable);
    }

    @Override
    public boolean canDrawOverOtherApps() {
        Log.i(TAG, "canDrawOverOtherApps");
        return surfaceManager.canDrawOverlays();
    }

    public void shutdown() {
        Log.i(TAG, "shutdown");
        nativeShutdownServer();
    }

    /**
     * Native method that starts server.
     */
    @SuppressWarnings("JavaJniMissingFunction")
    private native void nativeStartServer();

    /**
     * Native handling of receiving a surface: should convert it to an ANativeWindow then do stuff
     * with it.
     * <p>
     * Ignore warnings that this function is missing: it is not, it is just in a different module.
     * See `src/xrt/targets/service-lib/service_target.cpp` for the implementation.
     *
     * @param surface The surface to pass to native code
     * @todo figure out a good way to have a client ID
     */
    @SuppressWarnings("JavaJniMissingFunction")
    private native void nativeAppSurface(@NonNull Surface surface);

    /**
     * Native handling of receiving an FD for a new client: the FD should be used to start up the
     * rest of the native IPC code on that socket.
     * <p>
     * This is essentially the entry point for the monado service on Android: if it's already
     * running, this will be called in it. If it's not already running, a process will be created,
     * and this will be the first native code executed in that process.
     * <p>
     * Ignore warnings that this function is missing: it is not, it is just in a different module.
     * See `src/xrt/targets/service-lib/service_target.cpp` for the implementation.
     *
     * @param fd The incoming file descriptor: ownership is transferred to native code here.
     * @return 0 on success, anything else means the fd wasn't sent and ownership not transferred.
     * @todo figure out a good way to have a client ID
     */
    @SuppressWarnings("JavaJniMissingFunction")
    private native int nativeAddClient(int fd);

    /**
     * Native method that handles shutdown server.
     *
     * @return 0 on success; -1 means that server didn't start.
     */
    @SuppressWarnings("JavaJniMissingFunction")
    private native int nativeShutdownServer();
}
