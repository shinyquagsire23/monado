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

    private final Thread compositorThread = new Thread(
            this::threadEntry,
            "CompositorThread");
    private boolean started = false;

    private void launchThreadIfNeeded() {
        synchronized (compositorThread) {
            if (!started) {
                compositorThread.start();
                nativeWaitForServerStartup();
                started = true;
            }
        }
    }

    @Override
    public void connect(@NotNull ParcelFileDescriptor parcelFileDescriptor) {
        /// @todo launch this thread earlier/elsewhere
        launchThreadIfNeeded();
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
    }

    private void threadEntry() {
        Log.i(TAG, "threadEntry");
        nativeThreadEntry();
        Log.i(TAG, "native thread has exited");
    }

    /**
     * Native thread entry point.
     */
    @SuppressWarnings("JavaJniMissingFunction")
    private native void nativeThreadEntry();

    /**
     * Native method that waits until the server reports that it is, in fact, started up.
     */
    @SuppressWarnings("JavaJniMissingFunction")
    private native void nativeWaitForServerStartup();

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
}
