import 'package:flutter_test/flutter_test.dart';
import 'package:triengine_interop_flutter/triengine_interop_flutter.dart';
import 'package:triengine_interop_flutter/triengine_interop_flutter_platform_interface.dart';
import 'package:triengine_interop_flutter/triengine_interop_flutter_method_channel.dart';
import 'package:plugin_platform_interface/plugin_platform_interface.dart';

class MockTriengineInteropFlutterPluginPlatform
    with MockPlatformInterfaceMixin
    implements TriengineInteropFlutterPluginPlatform {

  @override
  Future<String?> getPlatformVersion() => Future.value('42');
}

void main() {
  final TriengineInteropFlutterPluginPlatform initialPlatform = TriengineInteropFlutterPluginPlatform.instance;

  test('$MethodChannelTriengineInteropFlutterPlugin is the default instance', () {
    expect(initialPlatform, isInstanceOf<MethodChannelTriengineInteropFlutterPlugin>());
  });

  test('getPlatformVersion', () async {
    TriengineInteropFlutterPlugin triengineInteropFlutterPlugin = TriengineInteropFlutterPlugin();
    MockTriengineInteropFlutterPluginPlatform fakePlatform = MockTriengineInteropFlutterPluginPlatform();
    TriengineInteropFlutterPluginPlatform.instance = fakePlatform;

    expect(await triengineInteropFlutterPlugin.getPlatformVersion(), '42');
  });
}
