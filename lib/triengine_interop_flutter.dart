import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'triengine_interop_flutter_platform_interface.dart';

// Mouse button constants
class MouseButton {
  static const int left = 0;   // MOUSE_L
  static const int right = 1;  // MOUSE_R  
  static const int middle = 2; // MOUSE_M
}

// Button action constants
class ButtonAction {
  static const int press = 0;   // ACTION_PRESS
  static const int release = 1; // ACTION_RELEASE
  static const int repeat = 2;  // ACTION_REPEAT
}

// Key button constants (mirror triengine_interop::surface::proto::key_button_type).
class KeyButton {
  static const int unknown = 0; // KEY_UNKNOWN

  static const int a = 1;  // KEY_A
  static const int b = 2;
  static const int c = 3;
  static const int d = 4;
  static const int e = 5;
  static const int f = 6;
  static const int g = 7;
  static const int h = 8;
  static const int i = 9;
  static const int j = 10;
  static const int k = 11;
  static const int l = 12;
  static const int m = 13;
  static const int n = 14;
  static const int o = 15;
  static const int p = 16;
  static const int q = 17;
  static const int r = 18;
  static const int s = 19;
  static const int t = 20;
  static const int u = 21;
  static const int v = 22;
  static const int w = 23;
  static const int x = 24;
  static const int y = 25;
  static const int z = 26; // KEY_Z

  static const int digit0 = 27; // KEY_0
  static const int digit1 = 28;
  static const int digit2 = 29;
  static const int digit3 = 30;
  static const int digit4 = 31;
  static const int digit5 = 32;
  static const int digit6 = 33;
  static const int digit7 = 34;
  static const int digit8 = 35;
  static const int digit9 = 36; // KEY_9

  static const int f1 = 37; // KEY_F1
  static const int f2 = 38;
  static const int f3 = 39;
  static const int f4 = 40;
  static const int f5 = 41;
  static const int f6 = 42;
  static const int f7 = 43;
  static const int f8 = 44;
  static const int f9 = 45;
  static const int f10 = 46;
  static const int f11 = 47;
  static const int f12 = 48; // KEY_F12

  static const int escape = 49;   // KEY_ESCAPE
  static const int back = 50;     // KEY_BACK (backspace)
  static const int enter = 51;    // KEY_RETURN
  static const int space = 52;    // KEY_SPACE
  static const int left = 53;     // KEY_LEFT
  static const int up = 54;       // KEY_UP
  static const int right = 55;    // KEY_RIGHT
  static const int down = 56;     // KEY_DOWN
  static const int multiply = 57; // KEY_MULTIPLY
  static const int add = 58;      // KEY_ADD
  static const int subtract = 59; // KEY_SUBTRACT
  static const int divide = 60;   // KEY_DIVIDE

  static const int tab = 61;      // KEY_TAB
  static const int delete = 62;   // KEY_DELETE
  static const int insert = 63;   // KEY_INSERT
  static const int home = 64;     // KEY_HOME
  static const int end = 65;      // KEY_END
  static const int pageUp = 66;   // KEY_PAGE_UP
  static const int pageDown = 67; // KEY_PAGE_DOWN

  static const int minus = 68;      // KEY_MINUS      - _
  static const int equal = 69;      // KEY_EQUAL      = +
  static const int comma = 70;      // KEY_COMMA      , <
  static const int period = 71;     // KEY_PERIOD     . >
  static const int semicolon = 72;  // KEY_SEMICOLON  ; :
  static const int slash = 73;      // KEY_SLASH      / ?
  static const int backslash = 74;  // KEY_BACKSLASH  \ |
  static const int lbracket = 75;   // KEY_LBRACKET   [ {
  static const int rbracket = 76;   // KEY_RBRACKET   ] }
  static const int apostrophe = 77; // KEY_APOSTROPHE ' "
  static const int grave = 78;      // KEY_GRAVE      ` ~
}

// Modifier constants
class Modifier {
  static const int none = 0;
  static const int shift = 1 << 0;     // MOD_KEY_SHIFT
  static const int ctrl = 1 << 1;      // MOD_KEY_CTRL
  static const int alt = 1 << 2;       // MOD_KEY_ALT
  static const int capslock = 1 << 3;  // MOD_KEY_CAPSLOCK
  static const int numlock = 1 << 4;   // MOD_KEY_NUMLOCK
  static const int mouseL = 1 << 13;   // MOD_MOUSE_L
  static const int mouseR = 1 << 14;   // MOD_MOUSE_R
  static const int mouseM = 1 << 15;   // MOD_MOUSE_M
}

/// Frame-rate cap value that disables the cap (no limit). Mirrors the native
/// `MAX_FPS_UNCAPPED`.
const int kMaxFpsUncapped = 0;

/// Compute an adaptive frame-rate cap from a display refresh rate.
///
/// Over-produces at 2x the refresh rate so consumed frames stay fresh, capped at 200 fps.
/// Returns 200 fps when the refresh rate is unknown or invalid. The result is a frame-rate
/// cap suitable for [TriengineInteropFlutterPlugin.changeMaxFps] or createSurface's `maxFps`.
int computeAdaptiveMaxFps(double displayRefreshRate) {
  const double overproduceFactor = 2.0;
  const int maxAdaptiveFps = 200;
  if (displayRefreshRate <= 1.0) {
    return maxAdaptiveFps;
  }
  final int requested = (displayRefreshRate * overproduceFactor).floor();
  return requested < maxAdaptiveFps ? requested : maxAdaptiveFps;
}

class TriengineInteropFlutterPlugin {
  static const MethodChannel _channel = MethodChannel('triengine_interop_flutter/channel');

  /// Create the surface and connect to the renderer.
  ///
  /// [maxFps] is the initial frame-rate cap applied at connect: 0 = uncapped, N = cap at N fps.
  Future<int?> createSurface(String rendererIpcServerName, Size initialSize, {int maxFps = 0}) async {
    try {
      final int? textureId = await _channel.invokeMethod('createSurface', {
        'ipcServerName': rendererIpcServerName,
        'width': initialSize.width.round(),
        'height': initialSize.height.round(),
        'maxFps': maxFps,
      });
      return textureId;
    } on PlatformException catch (e) {
      debugPrint("Failed to create surface: '${e.message}'.");
      return null;
    }
  }

  Future<void> destroySurface() async {
    try {
      await _channel.invokeMethod('destroySurface');
    } on PlatformException catch (e) {
      debugPrint("Failed to destroy surface: '${e.message}'.");
    }
  }

  Future<void> updateSurface() async {
    await _channel.invokeMethod('updateSurface');
  }

  Future<void> resizeSurface(Size newSize) async {
    await _channel.invokeMethod('resizeSurface', {
      'width': newSize.width.round(),
      'height': newSize.height.round(),
    });
  }

  /// Change the renderer frame-rate cap at runtime.
  ///
  /// [maxFps] 0 = uncapped, N = cap at N fps.
  Future<void> changeMaxFps(int maxFps) async {
    try {
      await _channel.invokeMethod('changeMaxFps', {
        'maxFps': maxFps,
      });
    } on PlatformException catch (e) {
      debugPrint("Failed to change max fps: '${e.message}'.");
    }
  }

  Future<void> sendMouseButtonEvent(Offset position, int button, int action, int mods) async {
    try {
      await _channel.invokeMethod('sendMouseButtonEvent', {
        'x': position.dx.round(),
        'y': position.dy.round(),
        'button': button,
        'action': action,
        'mods': mods,
      });
    } on PlatformException catch (e) {
      debugPrint("Failed to send mouse button event: '${e.message}'.");
    }
  }

  Future<void> sendMouseMoveEvent(Offset position, int mods) async {
    try {
      await _channel.invokeMethod('sendMouseMoveEvent', {
        'x': position.dx.round(),
        'y': position.dy.round(),
        'mods': mods,
      });
    } on PlatformException catch (e) {
      debugPrint("Failed to send mouse move event: '${e.message}'.");
    }
  }

  Future<void> sendMouseScrollEvent(Offset scrollDelta) async {
    try {
      // Convert Flutter scroll delta to GLFW-style normalized value
      // Flutter's scrollDelta.dy is in pixels, typically 120 pixels per scroll step
      // GLFW expects normalized values where 1.0 = one scroll step
      const kWheelDelta = 120.0; // Windows WHEEL_DELTA == 120
      final normalizedScrollOffsetY = -scrollDelta.dy / kWheelDelta; // Negate to match GLFW direction
      await _channel.invokeMethod('sendMouseScrollEvent', {
        'yoffset': normalizedScrollOffsetY,
      });
    } on PlatformException catch (e) {
      debugPrint("Failed to send mouse scroll event: '${e.message}'.");
    }
  }

  Future<void> sendKeyEvent(int key, int action, int mods) async {
    try {
      await _channel.invokeMethod('sendKeyEvent', {
        'key': key,
        'action': action,
        'mods': mods,
      });
    } on PlatformException catch (e) {
      debugPrint("Failed to send key event: '${e.message}'.");
    }
  }

  Future<String?> getPlatformVersion() {
    return TriengineInteropFlutterPluginPlatform.instance.getPlatformVersion();
  }
}
