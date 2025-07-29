import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:triengine_interop_flutter/triengine_interop_flutter_method_channel.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  MethodChannelTriengineInteropFlutterPlugin platform = MethodChannelTriengineInteropFlutterPlugin();
  const MethodChannel channel = MethodChannel('triengine_interop_flutter');

  setUp(() {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger.setMockMethodCallHandler(
      channel,
      (MethodCall methodCall) async {
        return '42';
      },
    );
  });

  tearDown(() {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger.setMockMethodCallHandler(channel, null);
  });

  test('getPlatformVersion', () async {
    expect(await platform.getPlatformVersion(), '42');
  });
}
