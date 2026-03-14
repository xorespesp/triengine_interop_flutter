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

class TriengineInteropFlutterPlugin {
  static const MethodChannel _channel = MethodChannel('triengine_interop_flutter/channel');

  Future<int?> createSurface(String rendererIpcServerName, Size initialSize) async {
    try {
      final int? textureId = await _channel.invokeMethod('createSurface', {
        'ipcServerName': rendererIpcServerName,
        'width': initialSize.width.round(),
        'height': initialSize.height.round(),
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

  Future<String?> getPlatformVersion() {
    return TriengineInteropFlutterPluginPlatform.instance.getPlatformVersion();
  }
}
