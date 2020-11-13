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

import androidx.annotation.Keep;
import androidx.annotation.NonNull;

import org.jetbrains.annotations.NotNull;

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

    @Override
    public void connect(@NotNull ParcelFileDescriptor parcelFileDescriptor) {
        Log.i(TAG, "connect");
        nativeAddClient(parcelFileDescriptor.detachFd());
    }

    @Override
    public void passAppSurface(Surface surface) {
        Log.i(TAG, "passAppSurface");
        if (surface == null) {
            Log.e(TAG, "Received a null Surface from the client!");
            return;
        }
        nativeAppSurface(surface);
    }

    /**
     * Native handling of receiving a surface: should convert it to an ANativeWindow then do stuff
     * with it.
     * <p>
     * Ignore warnings that this function is missing: it is not, it is just in a different module.
     * See `src/xrt/targets/service-lib/service_target.cpp` for the implementation.
     *
     * @param surface The surface to pass to native code
     * @todo figure out a good way to make the MonadoImpl pointer a client ID
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
     * @todo figure out a good way to make the MonadoImpl pointer a client ID
     */
    @SuppressWarnings("JavaJniMissingFunction")
    private native void nativeAddClient(int fd);
}
