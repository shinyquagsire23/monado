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
import android.view.Surface;

import androidx.annotation.Keep;

/**
 * Java implementation of the IMonado IPC interface.
 * <p>
 * (This is the server-side code.)
 * <p>
 * All this does is delegate calls to native JNI implementations
 */
@SuppressWarnings("JavaJniMissingFunction")
@Keep
public class MonadoImpl extends IMonado.Stub {

    static {
        // Load the shared library with the native parts of this class
        // This is the service-lib target.
        System.loadLibrary("monado-service");
    }

    public void connect(ParcelFileDescriptor parcelFileDescriptor) {
        nativeAddClient(parcelFileDescriptor.detachFd());
    }

    public void passAppSurface(Surface surface) {
        nativeAppSurface(surface);
    }

    /**
     * Native handling of receiving a surface: should convert it to an ANativeWindow then do stuff
     * with it.
     * <p>
     * Ignore Android Studio complaining that this function is missing: it is not, it is just in a
     * different module. See `src/xrt/targets/service-lib/lib.cpp` for the implementation.
     * (Ignore the warning saying that file isn't included in the build: it is, Android Studio
     * is just confused.)
     *
     * @param surface The surface to pass to native code
     * @todo figure out a good way to make the MonadoImpl pointer a client ID
     */
    private native void nativeAppSurface(Surface surface);

    /**
     * Native handling of receiving an FD for a new client: the FD should be used to start up the
     * rest of the native IPC code on that socket.
     * <p>
     * This is essentially the entry point for the monado service on Android: if it's already
     * running, this will be called in it. If it's not already running, a process will be created,
     * and this will be the first native code executed in that process.
     * <p>
     * Ignore Android Studio complaining that this function is missing: it is not, it is just in a
     * different module. See `src/xrt/targets/service-lib/lib.cpp` for the implementation.
     * (Ignore the warning saying that file isn't included in the build: it is, Android Studio
     * is just confused.)
     *
     * @param fd The incoming file descriptor: ownership is transferred to native code here.
     * @todo figure out a good way to make the MonadoImpl pointer a client ID
     */
    private native void nativeAddClient(int fd);
}
