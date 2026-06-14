import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/services.dart';
import 'package:triengine_interop_flutter/triengine_interop_flutter.dart';

// Helper method to calculate modifier keys from keyboard state
int _calculateModifierKeys() {
  int mods = Modifier.none;
  
  // Check keyboard modifier states using HardwareKeyboard
  if (HardwareKeyboard.instance.isLogicalKeyPressed(LogicalKeyboardKey.shiftLeft) ||
      HardwareKeyboard.instance.isLogicalKeyPressed(LogicalKeyboardKey.shiftRight)) {
    mods |= Modifier.shift;
  }
  
  if (HardwareKeyboard.instance.isLogicalKeyPressed(LogicalKeyboardKey.controlLeft) ||
      HardwareKeyboard.instance.isLogicalKeyPressed(LogicalKeyboardKey.controlRight)) {
    mods |= Modifier.ctrl;
  }
  
  if (HardwareKeyboard.instance.isLogicalKeyPressed(LogicalKeyboardKey.altLeft) ||
      HardwareKeyboard.instance.isLogicalKeyPressed(LogicalKeyboardKey.altRight)) {
    mods |= Modifier.alt;
  }
  
  if (HardwareKeyboard.instance.isLogicalKeyPressed(LogicalKeyboardKey.capsLock)) {
    mods |= Modifier.capslock;
  }
  
  if (HardwareKeyboard.instance.isLogicalKeyPressed(LogicalKeyboardKey.numLock)) {
    mods |= Modifier.numlock;
  }
  
  return mods;
}

// Helper method to calculate modifier keys including mouse buttons
int _calculateModifierKeysWithMouse(int mouseButtons) {
  int mods = _calculateModifierKeys();
  
  // Add mouse button modifiers
  if (mouseButtons & kPrimaryMouseButton != 0) { mods |= Modifier.mouseL; }
  if (mouseButtons & kSecondaryMouseButton != 0) { mods |= Modifier.mouseR; }
  if (mouseButtons & kMiddleMouseButton != 0) { mods |= Modifier.mouseM; }

  return mods;
}

// Maps Flutter logical keys to triengine proto key codes (KeyButton). Keys absent from
// this table are not forwarded to the renderer.
final Map<LogicalKeyboardKey, int> _logicalKeyToKeyButton = {
  LogicalKeyboardKey.keyA: KeyButton.a,
  LogicalKeyboardKey.keyB: KeyButton.b,
  LogicalKeyboardKey.keyC: KeyButton.c,
  LogicalKeyboardKey.keyD: KeyButton.d,
  LogicalKeyboardKey.keyE: KeyButton.e,
  LogicalKeyboardKey.keyF: KeyButton.f,
  LogicalKeyboardKey.keyG: KeyButton.g,
  LogicalKeyboardKey.keyH: KeyButton.h,
  LogicalKeyboardKey.keyI: KeyButton.i,
  LogicalKeyboardKey.keyJ: KeyButton.j,
  LogicalKeyboardKey.keyK: KeyButton.k,
  LogicalKeyboardKey.keyL: KeyButton.l,
  LogicalKeyboardKey.keyM: KeyButton.m,
  LogicalKeyboardKey.keyN: KeyButton.n,
  LogicalKeyboardKey.keyO: KeyButton.o,
  LogicalKeyboardKey.keyP: KeyButton.p,
  LogicalKeyboardKey.keyQ: KeyButton.q,
  LogicalKeyboardKey.keyR: KeyButton.r,
  LogicalKeyboardKey.keyS: KeyButton.s,
  LogicalKeyboardKey.keyT: KeyButton.t,
  LogicalKeyboardKey.keyU: KeyButton.u,
  LogicalKeyboardKey.keyV: KeyButton.v,
  LogicalKeyboardKey.keyW: KeyButton.w,
  LogicalKeyboardKey.keyX: KeyButton.x,
  LogicalKeyboardKey.keyY: KeyButton.y,
  LogicalKeyboardKey.keyZ: KeyButton.z,

  LogicalKeyboardKey.digit0: KeyButton.digit0,
  LogicalKeyboardKey.digit1: KeyButton.digit1,
  LogicalKeyboardKey.digit2: KeyButton.digit2,
  LogicalKeyboardKey.digit3: KeyButton.digit3,
  LogicalKeyboardKey.digit4: KeyButton.digit4,
  LogicalKeyboardKey.digit5: KeyButton.digit5,
  LogicalKeyboardKey.digit6: KeyButton.digit6,
  LogicalKeyboardKey.digit7: KeyButton.digit7,
  LogicalKeyboardKey.digit8: KeyButton.digit8,
  LogicalKeyboardKey.digit9: KeyButton.digit9,

  LogicalKeyboardKey.f1: KeyButton.f1,
  LogicalKeyboardKey.f2: KeyButton.f2,
  LogicalKeyboardKey.f3: KeyButton.f3,
  LogicalKeyboardKey.f4: KeyButton.f4,
  LogicalKeyboardKey.f5: KeyButton.f5,
  LogicalKeyboardKey.f6: KeyButton.f6,
  LogicalKeyboardKey.f7: KeyButton.f7,
  LogicalKeyboardKey.f8: KeyButton.f8,
  LogicalKeyboardKey.f9: KeyButton.f9,
  LogicalKeyboardKey.f10: KeyButton.f10,
  LogicalKeyboardKey.f11: KeyButton.f11,
  LogicalKeyboardKey.f12: KeyButton.f12,

  LogicalKeyboardKey.escape: KeyButton.escape,
  LogicalKeyboardKey.backspace: KeyButton.back,
  LogicalKeyboardKey.enter: KeyButton.enter,
  LogicalKeyboardKey.numpadEnter: KeyButton.enter,
  LogicalKeyboardKey.space: KeyButton.space,
  LogicalKeyboardKey.arrowLeft: KeyButton.left,
  LogicalKeyboardKey.arrowUp: KeyButton.up,
  LogicalKeyboardKey.arrowRight: KeyButton.right,
  LogicalKeyboardKey.arrowDown: KeyButton.down,
  LogicalKeyboardKey.numpadMultiply: KeyButton.multiply,
  LogicalKeyboardKey.numpadAdd: KeyButton.add,
  LogicalKeyboardKey.numpadSubtract: KeyButton.subtract,
  LogicalKeyboardKey.numpadDivide: KeyButton.divide,

  LogicalKeyboardKey.tab: KeyButton.tab,
  LogicalKeyboardKey.delete: KeyButton.delete,
  LogicalKeyboardKey.insert: KeyButton.insert,
  LogicalKeyboardKey.home: KeyButton.home,
  LogicalKeyboardKey.end: KeyButton.end,
  LogicalKeyboardKey.pageUp: KeyButton.pageUp,
  LogicalKeyboardKey.pageDown: KeyButton.pageDown,

  LogicalKeyboardKey.minus: KeyButton.minus,
  LogicalKeyboardKey.equal: KeyButton.equal,
  LogicalKeyboardKey.comma: KeyButton.comma,
  LogicalKeyboardKey.period: KeyButton.period,
  LogicalKeyboardKey.semicolon: KeyButton.semicolon,
  LogicalKeyboardKey.slash: KeyButton.slash,
  LogicalKeyboardKey.backslash: KeyButton.backslash,
  LogicalKeyboardKey.bracketLeft: KeyButton.lbracket,
  LogicalKeyboardKey.bracketRight: KeyButton.rbracket,
  LogicalKeyboardKey.quoteSingle: KeyButton.apostrophe, // apostrophe (' "), 0x27
  LogicalKeyboardKey.backquote: KeyButton.grave,
};

class TriengineSurface extends StatefulWidget {
  final String rendererIpcServerName;
  final Size size;
  final double devicePixelRatio;
  final FilterQuality filterQuality;

  const TriengineSurface({ 
    super.key, 
    required this.rendererIpcServerName,
    required this.size, 
    this.devicePixelRatio = 1.0, // Default to 1.0 for logical pixel coordinates
    this.filterQuality = FilterQuality.none, // Default to none for better performance
  });

  @override
  State<TriengineSurface> createState() => _TriengineSurfaceState();
}

class _TriengineSurfaceState extends State<TriengineSurface> with SingleTickerProviderStateMixin {
  final _interopPlugin = TriengineInteropFlutterPlugin();
  final GlobalKey _textureKey = GlobalKey();
  int? _textureId;
  Ticker? _ticker;
  
  // Track current mouse button state for move events
  int _currMouseButtonsState = 0;

  // Focus node for routing keyboard events to the surface. Focus is requested when the
  // user interacts with the surface (pointer down), so key events flow to the renderer.
  final FocusNode _focusNode = FocusNode(debugLabel: 'TriengineSurface');
  
  // Loading state management
  bool _isRecreating = false;
  bool _isResizing = false;
  
  // Debouncing timers
  Timer? _recreateDebounceTimer; // Debouncing timer for surface recreation
  Timer? _resizeDebounceTimer; // Debouncing timer for surface resize

  @override
  void initState() {
    super.initState();
    _createSurface().catchError((error) {
      debugPrint("Error initializing surface: $error");
    });
  }

  @override
  void didUpdateWidget(TriengineSurface oldWidget) {
    /**
     * didUpdateWidget():
     * 상위 위젯이 다시 렌더링되어 해당 StatefulWidget이 재구성될 때, didUpdateWidget() 메서드가 호출된다.
     * 여기서는 새로운 위젯과 이전 위젯의 차이점을 처리하는 로직을 구현할 수 있다.
     */
    super.didUpdateWidget(oldWidget);
    
    // Check if renderer server name has changed - requires surface recreation
    // TODO: Use controller pattern?
    if (widget.rendererIpcServerName != oldWidget.rendererIpcServerName) {
      debugPrint("Renderer server name changed from '${oldWidget.rendererIpcServerName}' to '${widget.rendererIpcServerName}' - scheduling recreation");
      
      // Debounce recreation to prevent rapid changes
      // Recreation is performed when idle for 300ms after the last change.
      _recreateDebounceTimer?.cancel(); // Cancel any pending recreate operations
      _recreateDebounceTimer = Timer(const Duration(milliseconds: 300), () {
        if (mounted) {
          _recreateSurfaceWithLoading().catchError((error) {
            debugPrint("Error recreating surface: $error");
            if (mounted) {
              setState(() {
                _isRecreating = false;
              });
            }
          });
        }
      });
      
      // Set loading state immediately
      setState(() {
        _isRecreating = true;
      });
      
      return; // Don't process other changes if recreating
    }
    
    if (_textureId != null && !_isRecreating) {
      // Check if Size or DevicePixelRatio has actually changed
      if (widget.size != oldWidget.size || widget.devicePixelRatio != oldWidget.devicePixelRatio) {
        debugPrint("Size or DevicePixelRatio changed from ${oldWidget.size} / ${oldWidget.devicePixelRatio} to ${widget.size} / ${widget.devicePixelRatio} - scheduling resize");
        
        // Debounce resize to prevent rapid changes during window dragging
        // Resize is performed when idle for 100ms after the last change.
        _resizeDebounceTimer?.cancel(); // Cancel any pending resize operations
        _resizeDebounceTimer = Timer(const Duration(milliseconds: 100), () {
          if (mounted && _textureId != null) {
            _resizeSurfaceWithLoading(widget.size).catchError((error) {
              debugPrint("Error resizing surface: $error");
              if (mounted) {
                setState(() {
                  _isResizing = false;
                });
              }
            });
          }
        });
        
        // Set resizing state immediately
        setState(() {
          _isResizing = true;
        });
      }
    }
  }

  @override
  void dispose() {
    // Cancel any pending recreation & resize operations
    _recreateDebounceTimer?.cancel();
    _resizeDebounceTimer?.cancel();
    
    _destroySurface().catchError((error) {
      debugPrint("Error deinitializing surface: $error");
    });
    _textureId = null;
    _ticker?.dispose();
    _focusNode.dispose();
    super.dispose();
  }

  // Helper method to convert logical Size to physical Size
  Size _getPhysicalSize(Size logicalSize) {
    return Size(
      (logicalSize.width * widget.devicePixelRatio).roundToDouble(),
      (logicalSize.height * widget.devicePixelRatio).roundToDouble(),
    );
  }

  // Helper method to convert logical Offset to physical Offset
  Offset _getPhysicalOffset(Offset logicalOffset) {
    return Offset(
      logicalOffset.dx * widget.devicePixelRatio,
      logicalOffset.dy * widget.devicePixelRatio,
    );
  }

  // Platform messages are asynchronous, so we initialize in an async method.
  Future<void> _createSurface() async {
    final currPhysicalWidgetSize = _getPhysicalSize(widget.size);
    debugPrint("Creating surface... (Physical Target: ${currPhysicalWidgetSize.width} x ${currPhysicalWidgetSize.height})");
    final newTextureId = await _interopPlugin.createSurface(
      widget.rendererIpcServerName,
      currPhysicalWidgetSize
    );

    setState(() {
      _textureId = newTextureId;
    });

    _ticker = this.createTicker((Duration now) {
      if (mounted && _textureId != null) {
        _interopPlugin.updateSurface();
      }
    });

    _ticker?.start();
  }

  // Recreate surface when renderer server name changes
  Future<void> _recreateSurfaceWithLoading() async {
    debugPrint("Recreating surface for new renderer: ${widget.rendererIpcServerName}");
    
    try {
      // Stop ticker first to prevent updates during recreation
      _ticker?.stop();
      _ticker?.dispose();
      _ticker = null;
      
      // Destroy existing surface & Create new surface with new renderer
      await _destroySurface();
      await _createSurface();
      
      // Reset loading state on success
      if (mounted) {
        setState(() {
          _isRecreating = false;
        });
      }
    } catch (error) {
      // Reset loading state on error
      if (mounted) {
        setState(() {
          _isRecreating = false;
        });
      }
      rethrow; // Re-throw to be caught by caller
    }
  }

  Future<void> _destroySurface() async {
    if (_textureId != null) {
      debugPrint("Destroying surface... (Texture ID: $_textureId)");
      await _interopPlugin.destroySurface();
      _textureId = null;
    }
  }

  // Resize surface with loading state management
  Future<void> _resizeSurfaceWithLoading(Size newSize) async {
    final newPhysicalSize = _getPhysicalSize(newSize);
    debugPrint("Resizing surface to physical $newPhysicalSize ... (Texture ID: $_textureId)");

    try {
      if (_textureId != null) {
        await _interopPlugin.resizeSurface(newPhysicalSize);
      }
      
      // Reset resizing state on success
      if (mounted) {
        setState(() {
          _isResizing = false;
        });
      }
    } catch (error) {
      // Reset resizing state on error
      if (mounted) {
        setState(() {
          _isResizing = false;
        });
      }
      rethrow; // Re-throw to be caught by caller
    }
  }

  // Helper method to convert global position to local texture widget coordinates
  Offset? _globalScreenPos2LocalTexturePos(Offset globalMousePos) {
    final RenderBox? renderBox = _textureKey.currentContext?.findRenderObject() as RenderBox?;
    if (renderBox == null) return null;
    
    final localPosition = renderBox.globalToLocal(globalMousePos);
    final currWidgetSize = widget.size; // Logical size of the widget
    
    // Check if the position is within the texture widget bounds
    // NOTE: Perform the bounds check using the logical size (`currWidgetSize`).
    if (localPosition.dx >= 0 && localPosition.dx < currWidgetSize.width && 
        localPosition.dy >= 0 && localPosition.dy < currWidgetSize.height) {
      
      // When passing the coordinates to C++, convert them to physical pixel coordinates before sending them.
      return _getPhysicalOffset(localPosition);
    }
    
    return null;
  }

  // Convert a global position to local texture coordinates, clamped to the widget
  // bounds. Unlike _globalScreenPos2LocalTexturePos this returns an in-bounds position
  // instead of null for positions outside the widget, so it is used for button-release
  // events that may occur outside the widget (e.g. a drag that ended off-widget). The
  // renderer ignores the position on button events anyway. Returns null only when the
  // render object is unavailable.
  Offset? _globalScreenPos2LocalTexturePosClamped(Offset globalMousePos) {
    final RenderBox? renderBox = _textureKey.currentContext?.findRenderObject() as RenderBox?;
    if (renderBox == null) return null;

    final localPosition = renderBox.globalToLocal(globalMousePos);
    final currWidgetSize = widget.size; // Logical size of the widget

    final clamped = Offset(
      localPosition.dx.clamp(0.0, currWidgetSize.width),
      localPosition.dy.clamp(0.0, currWidgetSize.height),
    );
    return _getPhysicalOffset(clamped);
  }

  // Forward release events for any buttons that went up while the pointer was outside
  // the texture region. A drag can end off-widget and the corresponding pointer-up may
  // never reach us; without this the renderer never sees the button-up and keeps
  // manipulating the scene after the cursor returns.
  void _syncReleasedButtons(int currentButtons, Offset globalPos) {
    final int released = _currMouseButtonsState & ~currentButtons;
    if (released == 0) { return; }

    final localPos = _globalScreenPos2LocalTexturePosClamped(globalPos);
    if (localPos == null) { return; }
    final int mods = _calculateModifierKeysWithMouse(currentButtons);

    if (released & kPrimaryMouseButton != 0) {
      _interopPlugin.sendMouseButtonEvent(localPos, MouseButton.left, ButtonAction.release, mods);
    }
    if (released & kSecondaryMouseButton != 0) {
      _interopPlugin.sendMouseButtonEvent(localPos, MouseButton.right, ButtonAction.release, mods);
    }
    if (released & kMiddleMouseButton != 0) {
      _interopPlugin.sendMouseButtonEvent(localPos, MouseButton.middle, ButtonAction.release, mods);
    }
  }

  // Translate a Flutter key event and forward it to the renderer. Returns handled for
  // keys we recognize so they aren't also consumed as text/shortcuts elsewhere.
  KeyEventResult _handleKeyEvent(FocusNode node, KeyEvent event) {
    final int? key = _logicalKeyToKeyButton[event.logicalKey];
    if (key == null) {
      return KeyEventResult.ignored; // Unmapped key: let Flutter handle it
    }

    final int action;
    if (event is KeyDownEvent) {
      action = ButtonAction.press;
    } else if (event is KeyRepeatEvent) {
      action = ButtonAction.repeat;
    } else if (event is KeyUpEvent) {
      action = ButtonAction.release;
    } else {
      return KeyEventResult.ignored;
    }

    final int mods = _calculateModifierKeys();
    _interopPlugin.sendKeyEvent(key, action, mods);
    return KeyEventResult.handled;
  }

  @override
  Widget build(BuildContext context) {
    final currWidgetSize = widget.size;
    
    return SizedBox(
      width: currWidgetSize.width,
      height: currWidgetSize.height,
      key: _textureKey,
      child: _buildContentWidget(context),
    );
  }

  Widget _buildContentWidget(BuildContext context) {
    if (_textureId == null) {
      return _buildLoadingIndicatorWidget(
        _isRecreating ? 'Switching renderer...' : 'Initializing surface...',
        Theme.of(context).textTheme.bodyMedium!,
      );
    }
    
    if (_isResizing) {
      // Show loading indicator while resizing
      return Stack(
        children: [
          // Show the disabled texture while resizing for smoother experience
          _buildDisabledSurfaceTextureWidget(),
          // Overlay resize indicator
          _buildOverlayLoadingIndicatorWidget(
            'Updating...', 
            TextStyle(
              color: Colors.white,
              fontSize: 12,
            ),
          ),
        ],
      );
    }
    
    return _buildSurfaceTextureWidget();
  }

  Widget _buildLoadingIndicatorWidget(String text, TextStyle textStyle) {
    return Center(
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          CircularProgressIndicator(),
          SizedBox(height: 16),
          Text(
            text,
            style: textStyle,
          ),
        ],
      ),
    );
  }

  Widget _buildOverlayLoadingIndicatorWidget(String text, TextStyle textStyle) {
    return Positioned(
      top: 8,
      right: 8,
      child: Container(
        padding: EdgeInsets.symmetric(horizontal: 8, vertical: 4),
        decoration: BoxDecoration(
          color: Colors.black54,
          borderRadius: BorderRadius.circular(4),
        ),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            SizedBox(
              width: 12,
              height: 12,
              child: CircularProgressIndicator(
                strokeWidth: 2,
                valueColor: AlwaysStoppedAnimation<Color>(Colors.white),
              ),
            ),
            SizedBox(width: 6),
            Text(
              text,
              style: textStyle,
            ),
          ],
        ),
      ),
    );
  }

  // Texture widget for the surface rendering, with disabled mouse interaction
  Widget _buildDisabledSurfaceTextureWidget() {
    return MouseRegion(
      onEnter: (event) {
        // Mouse entered the texture area
        //debugPrint("Mouse entered texture area (buttons: ${event.buttons})");
        _currMouseButtonsState = event.buttons;
      },
      onExit: (event) {
        // Mouse exited the texture area
        //debugPrint("Mouse exited texture area (buttons: ${event.buttons})");

        // Keep the pressed-button state while a button is held (see the enabled widget).
        if (event.buttons == 0) {
          _currMouseButtonsState = 0; // Reset only when no button is held
        }
      },
      child: Texture(
        textureId: _textureId!,
        filterQuality: widget.filterQuality,
      ),
    );
  }

  // Texture widget for the surface rendering, with enabled mouse interaction
  Widget _buildSurfaceTextureWidget() {
    return Focus(
      focusNode: _focusNode,
      onKeyEvent: _handleKeyEvent,
      child: MouseRegion(
      onEnter: (event) {
        // Mouse entered the texture area
        //debugPrint("Mouse entered texture area (buttons: ${event.buttons})");

        // Reconcile any buttons released while the pointer was outside this region so
        // the renderer doesn't stay stuck in a pressed state after the cursor returns.
        _syncReleasedButtons(event.buttons, event.position);
        _currMouseButtonsState = event.buttons;
      },
      onExit: (event) {
        // Mouse exited the texture area
        //debugPrint("Mouse exited texture area (buttons: ${event.buttons})");

        // Keep the pressed-button state while a drag is in progress: the Listener keeps
        // receiving the gesture outside this region, and clearing here would lose the
        // release and leave the renderer stuck in a pressed state.
        if (event.buttons == 0) {
          _currMouseButtonsState = 0; // Reset only when no button is held
        }
      },
      onHover: (event) {
        // Handle mouse move (no drag)
        // https://api.flutter.dev/flutter/widgets/MouseRegion-class.html
        //debugPrint("Mouse hover at position: ${event.position} (event.buttons: ${event.buttons})");

        // Send mouse move event only when no buttons are pressed
        if (_currMouseButtonsState == 0) {
          final localPos = _globalScreenPos2LocalTexturePos(event.position);
          if (localPos != null) {
            int mods = _calculateModifierKeys(); // Only keyboard modifiers when no mouse buttons
            _interopPlugin.sendMouseMoveEvent(localPos, mods);
          }
        }
      },
      child: Listener(
        onPointerMove: (event) {
          // Handle mouse move (drag)
          // https://api.flutter.dev/flutter/widgets/Listener-class.html
          //debugPrint("Mouse drag at position: ${event.position} (event.buttons: ${event.buttons})");

          final localPos = _globalScreenPos2LocalTexturePos(event.position);
          if (localPos == null) { return; }

          // Include current mouse button state in modifiers for drag operations
          int mods = _calculateModifierKeysWithMouse(_currMouseButtonsState);
          _interopPlugin.sendMouseMoveEvent(localPos, mods);
        },
        onPointerDown: (event) {
          // Handle mouse button press
          //debugPrint("Mouse button pressed at position: ${event.position} (event.buttons: ${event.buttons})");

          // Route keyboard input to the surface once the user interacts with it.
          _focusNode.requestFocus();

          final localPos = _globalScreenPos2LocalTexturePos(event.position);
          if (localPos == null) { return; }
        
          final int pressedButtons = event.buttons;

          // Determine which button was pressed - find the newly pressed button
          int? button;
          if (pressedButtons & kPrimaryMouseButton != 0) {
            button = MouseButton.left;
          } else if (pressedButtons & kSecondaryMouseButton != 0) {
            button = MouseButton.right;
          } else if (pressedButtons & kMiddleMouseButton != 0) {
            button = MouseButton.middle;
          }

          // Only send event if we detected a valid button
          if (button != null) {
            //debugPrint("Mouse button pressed: $button (buttons: ${event.buttons})");
            int mods = _calculateModifierKeysWithMouse(event.buttons);
            _interopPlugin.sendMouseButtonEvent(localPos, button, ButtonAction.press, mods);
          } else {
            debugPrint("Warning: Unknown button pressed (buttons: ${event.buttons})");
          }

          // Update current mouse button state after press
          _currMouseButtonsState = pressedButtons;
        },
        onPointerUp: (event) {
          // Handle mouse button release
          //debugPrint("Mouse button released at position: ${event.position} (event.buttons: ${event.buttons})");

          final int releasedButtons = _currMouseButtonsState & ~event.buttons;

          // Determine which button was released by comparing with previous state
          int? button;
          if (releasedButtons & kPrimaryMouseButton != 0) {
            button = MouseButton.left;
          } else if (releasedButtons & kSecondaryMouseButton != 0) {
            button = MouseButton.right;
          } else if (releasedButtons & kMiddleMouseButton != 0) {
            button = MouseButton.middle;
          } else {
            debugPrint("Warning: Unknown button released");
          }

          // Always forward the release, even when the pointer is outside the texture
          // bounds (e.g. the drag ended off-widget). Dropping it here would leave the
          // renderer stuck in a pressed state. The position is clamped to the widget;
          // the renderer ignores it on button events anyway.
          if (button != null) {
            final localPos = _globalScreenPos2LocalTexturePosClamped(event.position);
            if (localPos != null) {
              int mods = _calculateModifierKeysWithMouse(event.buttons);
              _interopPlugin.sendMouseButtonEvent(localPos, button, ButtonAction.release, mods);
            }
          }
          
          // Update current mouse button state after release
          _currMouseButtonsState = event.buttons;
        },
        onPointerSignal: (event) {
          // Handle mouse scroll
          if (event is PointerScrollEvent) {
            //debugPrint("Mouse scroll event (event.scrollDelta: ${event.scrollDelta.toString()})");

            final localPos = _globalScreenPos2LocalTexturePos(event.position);
            if (localPos == null) { return; }
            
            _interopPlugin.sendMouseScrollEvent(event.scrollDelta);
          }
        },
        child: Texture(
          textureId: _textureId!,
          filterQuality: widget.filterQuality,
        ),
      ),
      ),
    );
  }
} // class
