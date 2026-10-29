package org.androidtvps4.controller;

import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.provider.Settings;
import android.util.Log;

/** Turns the controller service on at every boot, keeping other enabled services. */
public final class EnableReceiver extends BroadcastReceiver {
  @Override public void onReceive(Context context, Intent intent) {
    String self = new ComponentName(context, ControllerService.class).flattenToString();
    try {
      String enabled = Settings.Secure.getString(context.getContentResolver(), Settings.Secure.ENABLED_ACCESSIBILITY_SERVICES);
      boolean present = false;
      if (enabled != null && !enabled.isEmpty())
        for (String service : enabled.split(":")) if (service.equalsIgnoreCase(self)) present = true;
      if (!present)
        Settings.Secure.putString(context.getContentResolver(), Settings.Secure.ENABLED_ACCESSIBILITY_SERVICES,
            enabled == null || enabled.isEmpty() ? self : enabled + ":" + self);
      Settings.Secure.putInt(context.getContentResolver(), Settings.Secure.ACCESSIBILITY_ENABLED, 1);
    } catch (SecurityException e) {
      Log.e("AtvController", "Cannot enable the controller service", e);
    }
  }
}
