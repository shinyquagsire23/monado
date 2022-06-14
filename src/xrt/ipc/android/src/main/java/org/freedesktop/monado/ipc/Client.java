// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Implementation of the Monado AIDL server
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup ipc_android
 */


package org.freedesktop.monado.ipc;

import android.app.Activity;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.IBinder;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.WindowManager;

import androidx.annotation.Keep;
import androidx.annotation.Nullable;

import org.freedesktop.monado.auxiliary.MonadoView;
import org.freedesktop.monado.auxiliary.NativeCounterpart;
import org.freedesktop.monado.auxiliary.SystemUiController;

import java.io.IOException;
import java.util.concurrent.Executors;

/**
 * Provides the client-side code to initiate connection to Monado IPC service.
 * <p>
 * This class will get loaded into the OpenXR client application by our native code.
 */
@Keep
public class Client implements ServiceConnection {
    private static final String TAG = "monado-ipc-client";
    /**
     * Used to block until binder is ready.
     */
    private final Object binderSync = new Object();
    /**
     * Keep track of the ipc_client_android instance over on the native side.
     */
    private final NativeCounterpart nativeCounterpart;
    /**
     * Pointer to local IPC proxy: calling methods on it automatically transports arguments across binder IPC.
     * <p>
     * May be null!
     */
    @Keep
    public IMonado monado = null;
    /**
     * Indicates that we tried to connect but failed.
     * <p>
     * Used to distinguish a "not yet fully connected" null monado member from a "tried and failed"
     * null monado member.
     */
    @Keep
    public boolean failed = false;
    /**
     * "Our" side of the socket pair - the other side is sent to the server automatically on connection.
     */
    private ParcelFileDescriptor fd = null;
    /**
     * Context provided by app.
     */
    private Context context = null;
    /**
     * Context of the runtime package
     */
    private Context runtimePackageContext = null;
    /**
     * Controll system ui visibility
     */
    private SystemUiController systemUiController = null;

    /**
     * Constructor
     *
     * @param nativePointer the corresponding native object's pointer.
     */
    @Keep
    public Client(long nativePointer) {
        this.nativeCounterpart = new NativeCounterpart(nativePointer);
        this.nativeCounterpart.markAsUsedByNativeCode();
    }

    private void shutdown() {
        monado = null;
        if (context != null) {
            context.unbindService(this);
        }

        if (fd != null) {
            try {
                fd.close();
            } catch (IOException e) {
                e.printStackTrace();
            }
            fd = null;
        }
    }

    /**
     * Let the native code notify us that it is no longer using this class.
     */
    @Keep
    public void markAsDiscardedByNative() {
        nativeCounterpart.markAsDiscardedByNative(TAG);
        shutdown();
    }

    /**
     * Bind to the Monado IPC service, and block until it is fully connected.
     * <p>
     * The IPC client code on Android should load this class (from the right package), instantiate
     * this class (retaining a reference to it!), and call this method.
     * <p>
     * This method must not be called from the main (UI) thread.
     *
     * @param context_    Context to use to make the connection. (We get the application context
     *                    from it.)
     * @param packageName The package name containing the Monado runtime. The caller is guaranteed
     *                    to know this because it had to load this class from that package.
     *                    There's a define in xrt_config_android.h to use for this.
     * @return the fd number - do not close! (dup if you want to be able to close it) Returns -1 if
     * something went wrong.
     * <p>
     * Various builds, variants, etc. will have different package names, but we must specify the
     * package name explicitly to avoid violating security restrictions.
     */
    @Keep
    public int blockingConnect(Context context_, String packageName) {
        Log.i(TAG, "blockingConnect");

        synchronized (binderSync) {
            if (!bind(context_, packageName)) {
                Log.e(TAG, "Bind failed immediately");
                // Bind failed immediately
                return -1;
            }
            try {
                binderSync.wait();
            } catch (InterruptedException e) {
                Log.e(TAG, "Interrupted: " + e.toString());
                return -1;
            }
        }

        if (monado == null) {
            Log.e(TAG, "Invalid binder object");
            return -1;
        }

        boolean surfaceCreated = false;
        Activity activity = null;
        if (context_ instanceof Activity) {
            activity = (Activity) context_;
        }

        try {
            // Determine whether runtime or client should create surface
            if (monado.canDrawOverOtherApps()) {
                WindowManager wm = (WindowManager) context_.getSystemService(Context.WINDOW_SERVICE);
                surfaceCreated = monado.createSurface(wm.getDefaultDisplay().getDisplayId(), false);
            } else {
                if (activity != null) {
                    Surface surface = attachViewAndGetSurface(activity);
                    surfaceCreated = (surface != null);
                    if (surfaceCreated) {
                        monado.passAppSurface(surface);
                    }
                }
            }
        } catch (RemoteException e) {
            e.printStackTrace();
        }

        if (!surfaceCreated) {
            Log.e(TAG, "Failed to create surface");
            handleFailure();
            return -1;
        }

        if (activity != null) {
            systemUiController = new SystemUiController(activity);
            systemUiController.hide();
        }

        // Create socket pair
        ParcelFileDescriptor theirs;
        ParcelFileDescriptor ours;
        try {
            ParcelFileDescriptor[] fds = ParcelFileDescriptor.createSocketPair();
            ours = fds[0];
            theirs = fds[1];
            monado.connect(theirs);
        } catch (IOException e) {
            e.printStackTrace();
            Log.e(TAG, "could not create socket pair: " + e.toString());
            handleFailure();
            return -1;
        } catch (RemoteException e) {
            e.printStackTrace();
            Log.e(TAG, "could not connect to service: " + e.toString());
            handleFailure();
            return -1;
        }

        fd = ours;
        Log.i(TAG, "Socket fd " + fd.getFd());
        return fd.getFd();
    }

    /**
     * Bind to the Monado IPC service - this asynchronously starts connecting (and launching the
     * service if it's not already running)
     *
     * @param context_    Context to use to make the connection. (We get the application context
     *                    from it.)
     * @param packageName The package name containing the Monado runtime. The caller is guaranteed
     *                    to know this because it had to load this class from that package.
     *                    There's a define in xrt_config_android.h to use for this.
     *                    <p>
     *                    Various builds, variants, etc. will have different package names, but we
     *                    must specify the package name explicitly to avoid violating security
     *                    restrictions.
     */
    public boolean bind(Context context_, String packageName) {
        Log.i(TAG, "bind");
        context = context_.getApplicationContext();
        if (context == null) {
            // in case app context returned null
            context = context_;
        }
        try {
            runtimePackageContext = context.createPackageContext(packageName,
                    Context.CONTEXT_IGNORE_SECURITY | Context.CONTEXT_INCLUDE_CODE);
        } catch (PackageManager.NameNotFoundException e) {
            e.printStackTrace();
            Log.e(TAG, "bind: Could not find package " + packageName);
            return false;
        }

        Intent intent = new Intent(BuildConfig.SERVICE_ACTION)
                .setPackage(packageName);

        if (!bindService(context, intent)) {
            Log.e(TAG,
                    "bindService: Service " + intent.toString() + " could not be found to bind!");
            return false;
        }

        // does not bind right away! This takes some time.
        return true;
    }

    private boolean bindService(Context context, Intent intent) {
        boolean result;
        int flags = Context.BIND_AUTO_CREATE | Context.BIND_IMPORTANT | Context.BIND_ABOVE_CLIENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            result = context.bindService(intent, flags | Context.BIND_INCLUDE_CAPABILITIES,
                    Executors.newSingleThreadExecutor(), this);
        } else {
            result = context.bindService(intent, this, flags);
        }

        return result;
    }

    /**
     * Some on-failure cleanup.
     */
    private void handleFailure() {
        failed = true;
        shutdown();
    }

    @Nullable
    private Surface attachViewAndGetSurface(Activity activity) {
        MonadoView monadoView = MonadoView.attachToActivity(activity);
        SurfaceHolder holder = monadoView.waitGetSurfaceHolder(2000);
        Surface surface = null;
        if (holder != null) {
            surface = holder.getSurface();
        }

        return surface;
    }

    /**
     * Handle the asynchronous connection of the binder IPC.
     *
     * @param name    should match the preceding intent, but not used.
     * @param service the associated service, which we cast in this function.
     */
    @Override
    public void onServiceConnected(ComponentName name, IBinder service) {
        Log.i(TAG, "onServiceConnected");

        synchronized (binderSync) {
            monado = IMonado.Stub.asInterface(service);
            binderSync.notify();
        }
    }

    /**
     * Handle asynchronous disconnect.
     *
     * @param name should match the preceding intent, but not used.
     */
    @Override
    public void onServiceDisconnected(ComponentName name) {
        Log.i(TAG, "onServiceDisconnected");
        shutdown();
        //! @todo tell C/C++ that the world is crumbling, then close the fd here.
    }

    /*
     * @todo do we need to watch for a disconnect here?
     *   https://stackoverflow.com/questions/18078914/notify-an-android-service-when-a-bound-client-disconnects
     *
     * Our existing native disconnect handling might be sufficient.
     */
}
