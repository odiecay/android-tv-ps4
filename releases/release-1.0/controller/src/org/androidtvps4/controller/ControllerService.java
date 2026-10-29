package org.androidtvps4.controller;

import android.accessibilityservice.AccessibilityService;
import android.content.SharedPreferences;
import android.content.pm.ApplicationInfo;
import android.content.pm.FeatureInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.hardware.input.InputManager;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.view.InputDevice;
import android.view.InputEvent;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityNodeInfo;
import android.widget.Toast;
import java.lang.reflect.Method;
import java.util.HashMap;
import java.util.Map;

/**
 * Apps built for a TV remote often take gamepad buttons and ignore them, so
 * Android's BUTTON_B to BACK fallback never happens. Outside games, this turns
 * cross and circle into the remote's DPAD_CENTER and BACK. Games, and apps the
 * user switches with Share + Options, receive the controller unchanged.
 */
public final class ControllerService extends AccessibilityService {
  private static final String TAG = "AtvController";
  private static final String[] GAME_PREFIXES = {
    "com.retroarch", "org.libretro.", "com.github.stenzek.duckstation", "org.ppsspp.",
    "org.dolphinemu.", "skyline.", "org.citra.", "io.github.lime3ds.", "xyz.aethersx2.",
    "com.explusalpha.", "com.valvesoftware.steamlink", "com.microsoft.xcloud",
    "com.nvidia.geforcenow", "com.playstation.remoteplay", "com.limelight", "com.moonlight",
  };
  private static final long TOGGLE_HOLD_MS = 1000;

  private final Map<String, Boolean> games = new HashMap<>();
  private final Handler handler = new Handler(Looper.getMainLooper());
  private SharedPreferences overrides;
  private InputManager input;
  private Method inject;
  private String lastPackage = "";
  private boolean shareDown, optionsDown;
  private int translatedCross = 0, translatedCircle = 0;  // remote keycode chosen at key down

  private final Runnable toggle = this::toggleMode;

  private void toggleMode() {
    String pkg = foregroundPackage();
    if (pkg.isEmpty()) return;
    boolean game = !isGame(pkg);
    overrides.edit().putBoolean(pkg, game).apply();
    Toast.makeText(this, game ? "Controller: game mode for this app" : "Controller: remote mode for this app",
        Toast.LENGTH_SHORT).show();
  }

  @Override protected void onServiceConnected() {
    overrides = getSharedPreferences("modes", MODE_PRIVATE);
    input = getSystemService(InputManager.class);
    try {
      inject = InputManager.class.getMethod("injectInputEvent", InputEvent.class, int.class);
    } catch (ReflectiveOperationException e) {
      Log.w(TAG, "Key injection unavailable; circle falls back to the Back action", e);
    }
  }

  @Override public void onAccessibilityEvent(AccessibilityEvent event) {
    if (event.getEventType() == AccessibilityEvent.TYPE_WINDOW_STATE_CHANGED && event.getPackageName() != null)
      lastPackage = event.getPackageName().toString();
  }

  @Override public void onInterrupt() {}

  @Override protected boolean onKeyEvent(KeyEvent event) {
    if ((event.getSource() & InputDevice.SOURCE_GAMEPAD) != InputDevice.SOURCE_GAMEPAD) return false;
    int code = event.getKeyCode();
    boolean down = event.getAction() == KeyEvent.ACTION_DOWN;

    if (code == KeyEvent.KEYCODE_BUTTON_SELECT || code == KeyEvent.KEYCODE_BUTTON_START) {
      if (code == KeyEvent.KEYCODE_BUTTON_SELECT) shareDown = down; else optionsDown = down;
      handler.removeCallbacks(toggle);
      if (shareDown && optionsDown) handler.postDelayed(toggle, TOGGLE_HOLD_MS);
      return false;
    }
    if (code == KeyEvent.KEYCODE_BUTTON_A) return remap(event, down, true);
    if (code == KeyEvent.KEYCODE_BUTTON_B) return remap(event, down, false);
    return false;
  }

  /**
   * Decide at key down and keep that decision for the matching key up. The
   * remote key is sent as one complete press when the button is released:
   * a separate down and up let the launcher treat it as a long press.
   */
  private boolean remap(KeyEvent event, boolean down, boolean cross) {
    if (down) {
      if (event.getRepeatCount() == 0) {
        String pkg = foregroundPackage();
        int target = pkg.isEmpty() || isGame(pkg) ? 0 : (cross ? KeyEvent.KEYCODE_DPAD_CENTER : KeyEvent.KEYCODE_BACK);
        if (cross) translatedCross = target; else translatedCircle = target;
      }
      return (cross ? translatedCross : translatedCircle) != 0;
    }
    int target = cross ? translatedCross : translatedCircle;
    if (cross) translatedCross = 0; else translatedCircle = 0;
    if (target == 0) return false;
    long now = SystemClock.uptimeMillis();
    boolean sent = send(remoteKey(now, KeyEvent.ACTION_DOWN, target)) && send(remoteKey(now, KeyEvent.ACTION_UP, target));
    if (Log.isLoggable(TAG, Log.DEBUG))
      Log.d(TAG, KeyEvent.keyCodeToString(event.getKeyCode()) + " -> " + KeyEvent.keyCodeToString(target) + " sent=" + sent);
    if (!sent && !cross) performGlobalAction(GLOBAL_ACTION_BACK);
    return true;
  }

  private static KeyEvent remoteKey(long time, int action, int code) {
    // Built the way the `input` shell command builds a remote key.
    return new KeyEvent(time, time, action, code, 0, 0, KeyCharacterMap.VIRTUAL_KEYBOARD, 0, 0,
        InputDevice.SOURCE_DPAD);
  }

  private boolean send(KeyEvent event) {
    if (inject == null) return false;
    try {
      return (Boolean) inject.invoke(input, event, 0);
    } catch (ReflectiveOperationException e) {
      Log.w(TAG, "Key injection failed", e);
      inject = null;
      return false;
    }
  }

  private String foregroundPackage() {
    AccessibilityNodeInfo root = getRootInActiveWindow();
    if (root != null && root.getPackageName() != null) return root.getPackageName().toString();
    return lastPackage;
  }

  private boolean isGame(String pkg) {
    if (overrides.contains(pkg)) return overrides.getBoolean(pkg, false);
    Boolean known = games.get(pkg);
    if (known == null) {
      known = detectGame(pkg);
      games.put(pkg, known);
      Log.i(TAG, pkg + (known ? ": game mode" : ": remote mode"));
    }
    return known;
  }

  @SuppressWarnings("deprecation")
  private boolean detectGame(String pkg) {
    for (String prefix : GAME_PREFIXES) if (pkg.startsWith(prefix)) return true;
    try {
      PackageInfo info = getPackageManager().getPackageInfo(pkg, PackageManager.GET_CONFIGURATIONS);
      ApplicationInfo app = info.applicationInfo;
      if (app.category == ApplicationInfo.CATEGORY_GAME || (app.flags & ApplicationInfo.FLAG_IS_GAME) != 0) return true;
      if (info.reqFeatures != null)
        for (FeatureInfo feature : info.reqFeatures)
          if ("android.hardware.gamepad".equals(feature.name)) return true;
    } catch (PackageManager.NameNotFoundException e) {
      return false;
    }
    return false;
  }
}
