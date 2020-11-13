// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Fragment to display the VR Mode status and actions.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */

package org.freedesktop.monado.android_common;

import android.annotation.SuppressLint;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.content.pm.ServiceInfo;
import android.content.res.Resources;
import android.os.Bundle;
import android.provider.Settings;
import android.service.vr.VrListenerService;
import android.util.AndroidRuntimeException;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.TextView;

import androidx.annotation.IntDef;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;

import org.freedesktop.monado.auxiliary.NameAndLogoProvider;
import org.freedesktop.monado.auxiliary.UiProvider;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.List;

import javax.inject.Inject;

import dagger.hilt.android.AndroidEntryPoint;


/**
 * A Fragment for displaying/affecting VR Listener status.
 */
@AndroidEntryPoint
public class VrModeStatus extends Fragment {
    public static final int STATUS_UNKNOWN = -2;
    public static final int STATUS_DISABLED = 0;
    public static final int STATUS_ENABLED = 1;
    public static final int STATUS_NOT_AVAIL = -1;
    private static final String TAG = "MonadoVrModeStatus";
    private static final String ARG_STATUS = "status";

    @Inject
    UiProvider uiProvider;

    @Inject
    NameAndLogoProvider nameAndLogoProvider;

    private @Status
    int status_ = STATUS_UNKNOWN;

    public VrModeStatus() {
        // Required empty public constructor
    }

    /**
     * Get the ComponentName for a VrListenerService
     *
     * @param resolveInfo a ResolveInfo from PackageManager.queryIntentServices() with an
     *                    android.service.vr.VrListenerService Intent and
     *                    PackageManager.GET_META_DATA
     * @return the ComponentName, or null if resolveInfo.serviceInfo is null
     */
    private static ComponentName getVrListener(@NonNull ResolveInfo resolveInfo) {
        ServiceInfo serviceInfo = resolveInfo.serviceInfo;
        if (serviceInfo == null) {
            return null;
        }
        return new ComponentName(serviceInfo.packageName, serviceInfo.name);
    }

    private static @Nullable
    ComponentName findOurselves(@NonNull List<ResolveInfo> resolutions, @NonNull String packageName) {
        for (ResolveInfo resolveInfo : resolutions) {
            ComponentName componentName = getVrListener(resolveInfo);
            if (componentName == null) continue;
            Log.i(TAG, "Looking at VrListener: " + componentName.getPackageName());
            if (componentName.getPackageName().equals(packageName)) {
                // we found us
                return componentName;
            }
        }
        return null;
    }

    /**
     * Determine the VR mode status of this package.
     *
     * @param context     A context to look up package manager info about this package.
     * @param packageName The current package name (usually BuildConfig.APPLICATION_ID)
     * @return the VR mode status
     */
    public static @Status
    int detectStatus(@NonNull Context context, @NonNull String packageName) {
        Intent intent = new Intent(VrListenerService.SERVICE_INTERFACE);
        PackageManager packageManager = context.getPackageManager();
        // Suppression because we only care about finding out about our own package.
        @SuppressLint("QueryPermissionsNeeded")
        List<ResolveInfo> resolutions = packageManager.queryIntentServices(intent,
                PackageManager.GET_META_DATA);
        if (resolutions == null || resolutions.isEmpty()) {
            return STATUS_NOT_AVAIL;
        }
        ComponentName us = findOurselves(resolutions, packageName);
        if (us == null) {
            Log.w(TAG, "Could not find ourselves in the list of VrListenerService implementations! " + packageName);
            return STATUS_NOT_AVAIL;
        }

        try {
            boolean enabled = VrListenerService.isVrModePackageEnabled(context, us);
            return enabled ? STATUS_ENABLED : STATUS_DISABLED;
        } catch (Exception e) {
            return STATUS_NOT_AVAIL;
        }
    }

    /**
     * Use this factory method to create a new instance of
     * this fragment using the provided parameters.
     *
     * @param status The VR Mode status. See detectStatus()
     * @return A new instance of fragment VrModeStatus.
     */
    public static VrModeStatus newInstance(@Status int status) {
        VrModeStatus fragment = new VrModeStatus();
        Bundle args = new Bundle();
        args.putInt(ARG_STATUS, status);
        fragment.setArguments(args);
        return fragment;
    }

    private static void launchVrSettings(Context context) {
        Intent intent = new Intent(Settings.ACTION_VR_LISTENER_SETTINGS);
        try {
            context.startActivity(intent);
        } catch (AndroidRuntimeException exception) {
            Log.w("Monado", "Got exception trying to start VR listener settings: " + exception.toString());
        }
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (getArguments() != null) {
            status_ = getArguments().getInt(ARG_STATUS);
        }
    }

    private void updateState(View v) {
        TextView textEnabledDisabled = v.findViewById(R.id.textEnabledDisabled);
        Button button = v.findViewById(R.id.btnLaunchVrSettings);
        Resources res = getResources();
        switch (status_) {
            case STATUS_DISABLED:
                textEnabledDisabled.setText(R.string.vr_mode_disabled);
                textEnabledDisabled.setVisibility(View.VISIBLE);
                button.setVisibility(View.VISIBLE);
                break;
            case STATUS_ENABLED:
                textEnabledDisabled.setText(R.string.vr_mode_enabled);
                textEnabledDisabled.setVisibility(View.VISIBLE);
                button.setVisibility(View.VISIBLE);
                break;
            case STATUS_NOT_AVAIL:
                textEnabledDisabled.setText(
                        res.getString(R.string.vr_mode_not_avail,
                                nameAndLogoProvider.getLocalizedRuntimeName()));
                textEnabledDisabled.setVisibility(View.VISIBLE);
                button.setVisibility(View.GONE);
                break;
            case STATUS_UNKNOWN:
                textEnabledDisabled.setVisibility(View.GONE);
                button.setVisibility(View.GONE);
                break;

            default:
                throw new IllegalStateException("Unexpected value: " + status_);
        }
    }

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container,
                             Bundle savedInstanceState) {
        // Inflate the layout for this fragment
        View view = inflater.inflate(R.layout.fragment_vr_mode_status, container, false);
        updateState(view);
        view.findViewById(R.id.btnLaunchVrSettings).setOnClickListener(v -> launchVrSettings(v.getContext()));
        return view;
    }

    @IntDef(value = {STATUS_UNKNOWN, STATUS_DISABLED, STATUS_ENABLED, STATUS_NOT_AVAIL})
    @Retention(RetentionPolicy.SOURCE)
    public @interface Status {
    }

}
