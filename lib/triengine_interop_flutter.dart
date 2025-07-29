
import 'package:flutter/services.dart';
import 'triengine_interop_flutter_platform_interface.dart';

class TriengineInteropFlutterPlugin {
  static const MethodChannel _channel = MethodChannel('triengine_interop_flutter/channel');

  Future<int?> createSurface(int width, int height) async {
    try {
      final int? textureId = await _channel.invokeMethod('createSurface', {
        'width': width,
        'height': height,
      });
      return textureId;
    } on PlatformException catch (e) {
      print("Failed to create surface: '${e.message}'.");
      return null;
    }
  }

  Future<void> destroySurface() async {
    try {
      await _channel.invokeMethod('destroySurface');
    } on PlatformException catch (e) {
      print("Failed to destroy surface: '${e.message}'.");
    }
  }

  Future<void> updateSurface() async {
    await _channel.invokeMethod('updateSurface');
  }

  Future<void> resizeSurface(int newWidth, int newHeight) async {
    await _channel.invokeMethod('resizeSurface', {
      'width': newWidth,
      'height': newHeight,
    });
  }

  Future<String?> getPlatformVersion() {
    return TriengineInteropFlutterPluginPlatform.instance.getPlatformVersion();
  }
}
