import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'dart:async';

import 'package:flutter/services.dart';
import 'package:triengine_interop_flutter/triengine_interop_flutter.dart';

void main() {
  runApp(const MyApp());
}

class MyApp extends StatefulWidget {
  const MyApp({super.key});

  @override
  State<MyApp> createState() => _MyAppState();
}

class _MyAppState extends State<MyApp> with SingleTickerProviderStateMixin {
  int? _textureId;
  Ticker? _ticker;
  final _interopPlugin = TriengineInteropFlutterPlugin();

  @override
  void initState() {
    super.initState();
    initPlugin().catchError((error) {
      print("Error initializing plugin: $error");
    });
  }

  @override
  void dispose() {
    _interopPlugin.destroySurface().then((_) {
      print("plugin deinitialized");
    }).catchError((error) {
      print("Error destroying plugin: $error");
    });
    _textureId = null;
    _ticker?.dispose();
    super.dispose();
  }

  // Platform messages are asynchronous, so we initialize in an async method.
  Future<void> initPlugin() async {

    final textureId = await _interopPlugin.createSurface(640, 640);

    if (!mounted) { return; }

    setState(() {
      _textureId = textureId;
    });

    _ticker = this.createTicker((Duration now) {
      if (mounted && _textureId != null) {
        _interopPlugin.updateSurface();
      }
    });

    _ticker?.start();
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      home: Scaffold(
        backgroundColor: Colors.black,
        appBar: AppBar(
          title: const Text('Flutter + Triengine Interoperability Demo'),
        ),
        body: Center(
          child: SizedBox(
            width: 640,
            height: 640,
            child: _textureId != null
                ? Texture(
                    textureId: _textureId!,
                    filterQuality: FilterQuality.none,
                  )
                : const CircularProgressIndicator(),
          ),
        ),
      ),
    );
  }
}
