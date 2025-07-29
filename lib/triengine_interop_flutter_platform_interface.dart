import 'package:plugin_platform_interface/plugin_platform_interface.dart';

import 'triengine_interop_flutter_method_channel.dart';

abstract class TriengineInteropFlutterPluginPlatform extends PlatformInterface {
  /// Constructs a TriengineInteropFlutterPluginPlatform.
  TriengineInteropFlutterPluginPlatform() : super(token: _token);

  static final Object _token = Object();

  static TriengineInteropFlutterPluginPlatform _instance = MethodChannelTriengineInteropFlutterPlugin();

  /// The default instance of [TriengineInteropFlutterPluginPlatform] to use.
  ///
  /// Defaults to [MethodChannelTriengineInteropFlutterPlugin].
  static TriengineInteropFlutterPluginPlatform get instance => _instance;

  /// Platform-specific implementations should set this with their own
  /// platform-specific class that extends [TriengineInteropFlutterPluginPlatform] when
  /// they register themselves.
  static set instance(TriengineInteropFlutterPluginPlatform instance) {
    PlatformInterface.verifyToken(instance, _token);
    _instance = instance;
  }

  Future<String?> getPlatformVersion() {
    throw UnimplementedError('platformVersion() has not been implemented.');
  }
}
