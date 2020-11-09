// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Utility class to deal with having a native-code counterpart object
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_android_java
 */

package org.freedesktop.monado.auxiliary;

import android.util.Log;

import androidx.annotation.CheckResult;
import androidx.annotation.NonNull;

import java.security.InvalidParameterException;

/**
 * Object that tracks the native counterpart object for a type. Must be initialized on construction,
 * and may have its native code destroyed/discarded, but may not "re-seat" it to new native code
 * pointer.
 * <p>
 * Use as a member of any type with a native counterpart (a native-allocated-and-owned object that
 * holds a reference to the owning class). Include the following field and delegating method to use
 * (note: assumes you have a tag for logging purposes as TAG)
 * <p>
 * <pre>
 * private final NativeCounterpart nativeCounterpart;
 *
 * &#64;Keep
 * public void markAsDiscardedByNative() {
 *     nativeCounterpart.markAsDiscardedByNative(TAG);
 * }
 * </pre>
 * Then, initialize it in your constructor, call {@code markAsUsedByNativeCode()} where desired
 * (often in your constructor), and call {@code getNativePointer()} and
 * {@code blockUntilNativeDiscard()} as needed.
 * <p>
 * Your native code can use this to turn a void* into a jlong:
 * {@code static_cast<long long>(reinterpret_cast<intptr_t>(nativePointer))}
 */
public final class NativeCounterpart {
    /**
     * Guards the usedByNativeCodeSync.
     */
    private final Object usedByNativeCodeSync = new Object();

    /**
     * Indicates if the containing object is in use by native code.
     * <p>
     * Guarded by usedByNativeCodeSync.
     */
    private boolean usedByNativeCode = false;

    /**
     * Contains the pointer to the native counterpart object.
     */
    private long nativePointer = 0;

    /**
     * Constructor
     *
     * @param nativePointer The native pointer, cast appropriately. Must be non-zero. Can cast like:
     *                      {@code static_cast<long long>(reinterpret_cast<intptr_t>(nativePointer))}
     */
    public NativeCounterpart(long nativePointer) throws InvalidParameterException {
        if (nativePointer == 0) {
            throw new InvalidParameterException("nativePointer must not be 0");
        }
        this.nativePointer = nativePointer;
    }

    /**
     * Set the flag to indicate that native code is using this. Only call this once, probably in
     * your constructor unless you have a good reason to call it elsewhere.
     */
    public void markAsUsedByNativeCode() {
        synchronized (usedByNativeCodeSync) {
            assert nativePointer != 0;
            usedByNativeCode = true;
            usedByNativeCodeSync.notifyAll();
        }
    }

    /**
     * Change the flag and notify those waiting on it, to indicate that native code is done with
     * this object.
     *
     * @param TAG Your owning class's logging tag
     */
    public void markAsDiscardedByNative(String TAG) {
        synchronized (usedByNativeCodeSync) {
            if (!usedByNativeCode) {
                Log.w(TAG,
                        "This should not have happened: Discarding by native code, but not marked as used!");
            }
            usedByNativeCode = false;
            nativePointer = 0;
            usedByNativeCodeSync.notifyAll();
        }
    }

    /**
     * Retrieve the native pointer value. Will be 0 if discarded by native code!.
     *
     * @return pointer (cast as a long)
     */
    public long getNativePointer() {
        synchronized (usedByNativeCodeSync) {
            return nativePointer;
        }
    }

    /**
     * Wait until {@code markAsDiscardedByNative} has been called indicating that the native code is
     * done with this. Be sure to check the result!
     *
     * @param TAG Your owning class's logging tag
     * @return true if this class has successfully been discarded by native.
     */
    @CheckResult
    public boolean blockUntilNativeDiscard(@NonNull String TAG) {
        try {
            synchronized (usedByNativeCodeSync) {
                while (usedByNativeCode) {
                    usedByNativeCodeSync.wait();
                }
                return true;
            }
        } catch (InterruptedException e) {
            e.printStackTrace();
            Log.i(TAG,
                    "Interrupted while waiting for native code to finish up: " + e.toString());
            return false;
        }

    }
}
