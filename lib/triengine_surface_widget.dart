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

class TriengineSurface extends StatefulWidget {
  final String rendererIpcServerName;
  final Size size;

  const TriengineSurface({ 
    super.key, 
    required this.rendererIpcServerName,
    required this.size, 
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
      print("Error initializing surface: $error");
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
      print("Renderer server name changed from '${oldWidget.rendererIpcServerName}' to '${widget.rendererIpcServerName}' - scheduling recreation");
      
      // Debounce recreation to prevent rapid changes
      // Recreation is performed when idle for 300ms after the last change.
      _recreateDebounceTimer?.cancel(); // Cancel any pending recreate operations
      _recreateDebounceTimer = Timer(const Duration(milliseconds: 300), () {
        if (mounted) {
          _recreateSurfaceWithLoading().catchError((error) {
            print("Error recreating surface: $error");
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
      // Check if size has actually changed
      if (widget.size != oldWidget.size) {
        print("Size changed from ${oldWidget.size} to ${widget.size} - scheduling resize");
        
        // Debounce resize to prevent rapid changes during window dragging
        // Resize is performed when idle for 100ms after the last change.
        _resizeDebounceTimer?.cancel(); // Cancel any pending resize operations
        _resizeDebounceTimer = Timer(const Duration(milliseconds: 100), () {
          if (mounted && _textureId != null) {
            _resizeSurfaceWithLoading(widget.size).catchError((error) {
              print("Error resizing surface: $error");
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
      print("Error deinitializing surface: $error");
    });
    _textureId = null;
    _ticker?.dispose();
    super.dispose();
  }

  // Platform messages are asynchronous, so we initialize in an async method.
  Future<void> _createSurface() async {
    print("Creating surface... (Initial Size: ${widget.size})");

    final currWidgetSize = widget.size;
    final newTextureId = await _interopPlugin.createSurface(
      widget.rendererIpcServerName,
      currWidgetSize
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
    print("Recreating surface for new renderer: ${widget.rendererIpcServerName}");
    
    try {
      // Stop ticker first to prevent updates during recreation
      _ticker?.stop();
      
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
      print("Destroying surface... (Texture ID: $_textureId)");
      await _interopPlugin.destroySurface();
      _textureId = null;
    }
  }

  // Resize surface with loading state management
  Future<void> _resizeSurfaceWithLoading(Size newSize) async {
    print("Resizing surface to $newSize ... (Texture ID: $_textureId)");
    
    try {
      if (_textureId != null) {
        await _interopPlugin.resizeSurface(newSize);
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
    final currWidgetSize = widget.size;
    
    // Check if the position is within the texture widget bounds
    if (localPosition.dx >= 0 && localPosition.dx < currWidgetSize.width && 
        localPosition.dy >= 0 && localPosition.dy < currWidgetSize.height) {
      return localPosition;
    }
    
    return null;
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
        //print("Mouse entered texture area (buttons: ${event.buttons})");
        _currMouseButtonsState = event.buttons;
      },
      onExit: (event) {
        // Mouse exited the texture area
        //print("Mouse exited texture area (buttons: ${event.buttons})");
        _currMouseButtonsState = 0; // Reset current mouse button state
      },
      child: Texture(
        textureId: _textureId!,
        filterQuality: FilterQuality.high,
      ),
    );
  }

  // Texture widget for the surface rendering, with enabled mouse interaction
  Widget _buildSurfaceTextureWidget() {
    return MouseRegion(
      onEnter: (event) {
        // Mouse entered the texture area
        //print("Mouse entered texture area (buttons: ${event.buttons})");
        _currMouseButtonsState = event.buttons;
      },
      onExit: (event) {
        // Mouse exited the texture area
        //print("Mouse exited texture area (buttons: ${event.buttons})");
        _currMouseButtonsState = 0; // Reset current mouse button state
      },
      onHover: (event) {
        // Handle mouse move (no drag)
        // https://api.flutter.dev/flutter/widgets/MouseRegion-class.html
        //print("Mouse hover at position: ${event.position} (event.buttons: ${event.buttons})");

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
          //print("Mouse drag at position: ${event.position} (event.buttons: ${event.buttons})");

          final localPos = _globalScreenPos2LocalTexturePos(event.position);
          if (localPos == null) { return; }

          // Include current mouse button state in modifiers for drag operations
          int mods = _calculateModifierKeysWithMouse(_currMouseButtonsState);
          _interopPlugin.sendMouseMoveEvent(localPos, mods);
        },
        onPointerDown: (event) {
          // Handle mouse button press
          //print("Mouse button pressed at position: ${event.position} (event.buttons: ${event.buttons})");

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
            //print("Mouse button pressed: $button (buttons: ${event.buttons})");
            int mods = _calculateModifierKeysWithMouse(event.buttons);
            _interopPlugin.sendMouseButtonEvent(localPos, button, ButtonAction.press, mods);
          } else {
            print("Warning: Unknown button pressed (buttons: ${event.buttons})");
          }

          // Update current mouse button state after press
          _currMouseButtonsState = pressedButtons;
        },
        onPointerUp: (event) {
          // Handle mouse button release
          //print("Mouse button released at position: ${event.position} (event.buttons: ${event.buttons})");

          final localPos = _globalScreenPos2LocalTexturePos(event.position);
          if (localPos == null) { return; }

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
            print("Warning: Unknown button released");
          }
          
          // Only send event if we detected a valid button release
          if (button != null) {
            int mods = _calculateModifierKeysWithMouse(event.buttons);
            _interopPlugin.sendMouseButtonEvent(localPos, button, ButtonAction.release, mods);
          }
          
          // Update current mouse button state after release
          _currMouseButtonsState = event.buttons;
        },
        onPointerSignal: (event) {
          // Handle mouse scroll
          if (event is PointerScrollEvent) {
            //print("Mouse scroll event (event.scrollDelta: ${event.scrollDelta.toString()})");

            final localPos = _globalScreenPos2LocalTexturePos(event.position);
            if (localPos == null) { return; }

            _interopPlugin.sendMouseScrollEvent(event.scrollDelta);
          }
        },
        child: Texture(
          textureId: _textureId!,
          filterQuality: FilterQuality.high,
        ),
      ),
    );
  }
} // class
