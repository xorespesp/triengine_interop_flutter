import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

import 'triengine_interop_flutter_platform_interface.dart';

/// An implementation of [TriengineInteropFlutterPluginPlatform] that uses method channels.
class MethodChannelTriengineInteropFlutterPlugin extends TriengineInteropFlutterPluginPlatform {
  /// The method channel used to interact with the native platform.
  @visibleForTesting
  final methodChannel = const MethodChannel('triengine_interop_flutter');

  @override
  Future<String?> getPlatformVersion() async {
    final version = await methodChannel.invokeMethod<String>('getPlatformVersion');
    return version;
  }
}
