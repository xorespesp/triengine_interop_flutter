
import 'triengine_interop_flutter_platform_interface.dart';

class TriengineInteropFlutterPlugin {
  Future<String?> getPlatformVersion() {
    return TriengineInteropFlutterPluginPlatform.instance.getPlatformVersion();
  }
}
