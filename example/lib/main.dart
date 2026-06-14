import 'package:flutter/material.dart';
import 'package:triengine_interop_flutter/triengine_surface_widget.dart';

void main() {
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
    const MyApp({super.key});

    @override
    Widget build(BuildContext context) {
        return MaterialApp(
          theme: ThemeData(
            brightness: Brightness.dark,
            primaryColor: Colors.black12,
            scaffoldBackgroundColor: Colors.black,
            appBarTheme: const AppBarTheme(
              backgroundColor: Colors.black12,
              titleTextStyle: TextStyle(color: Colors.white, fontSize: 20),
            ),
          ),
          home: Scaffold(
            appBar: AppBar(
              title: const Text('Flutter + Triengine Interoperability Demo'),
              titleTextStyle: const TextStyle(
                fontSize: 20,
              ),
            ),
            body: const HomePage(),
          ),
        );
    }
}

class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  final TextEditingController _serverNameController = TextEditingController(
    text: "triengine-interproc-renderer-srv"
  );

  @override
  void dispose() {
    _serverNameController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(32.0),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          crossAxisAlignment: CrossAxisAlignment.center,
          children: [
            Wrap(
              alignment: WrapAlignment.center,
              spacing: 16,
              children: [
                SizedBox(
                  width: 300,
                  child: TextField(
                    controller: _serverNameController,
                    decoration: const InputDecoration(
                      labelText: 'Renderer Server Name',
                      border: OutlineInputBorder(),
                      fillColor: Colors.black,
                      filled: true,
                    ),
                    style: const TextStyle(color: Colors.white),
                  ),
                ),
                SizedBox(
                  width: 120,
                  height: 50,
                  child: ElevatedButton(
                    onPressed: () {
                      Navigator.push(
                        context, 
                        MaterialPageRoute(
                          builder: (context) => TriengineScenePage(
                            rendererIpcServerName: _serverNameController.text,
                          ),
                        ),
                      );
                    },
                    style: ElevatedButton.styleFrom(
                      textStyle: const TextStyle(fontSize: 16),
                      shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(12),
                      ),
                    ),
                    child: const Text('Connect'),
                  ),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

class TriengineScenePage extends StatefulWidget {
  final String rendererIpcServerName;
  
  const TriengineScenePage({
    super.key,
    required this.rendererIpcServerName,
  });

  @override
  State<TriengineScenePage> createState() => _TriengineScenePageState();
}

class _TriengineScenePageState extends State<TriengineScenePage> with TickerProviderStateMixin {
  static const double _minRightPanelWidthRatio = 0.2; // 우측 패널의 최소 너비 (화면 너비의 20%)
  static const double _maxRightPanelWidthRatio = 0.8; // 우측 패널의 최대 너비 (화면 너비의 80%)
  static const double _defaultRightPanelWidth = 300.0;
  static const double _splitterWidth = 4.0;
  static const Duration _animationDuration = Duration(milliseconds: 200);

  // 런타임 프레임율 상한 프리셋. 
  // null 값은 ADAPTIVE를 의미 (네이티브 consumer가 로컬 디스플레이 주사율로 해석)
  static const List<({String label, int? value})> _maxFpsPresets = [
    (label: 'ADAPTIVE', value: null),
    (label: '30 fps', value: 30),
    (label: '60 fps', value: 60),
    (label: '90 fps', value: 90),
    (label: '120 fps', value: 120),
    (label: '144 fps', value: 144),
    (label: '165 fps', value: 165),
    (label: '200 fps', value: 200),
    (label: '240 fps', value: 240),
  ];

  late AnimationController _animationController;
  late Animation<double> _slideAnimation;
  bool _isRightPanelOpened = false; // 우측 패널의 열림 상태 여부 플래그
  bool _isSplitterMouseHovered = false; // splitter 위에 마우스가 hover되어 있는지 여부 플래그
  double _currRightPanelWidth = _defaultRightPanelWidth; // 메뉴 버튼 클릭시 열릴 (혹은 현재 열려있는) 우측 패널의 너비. splitter로 조절 가능

  final _surfaceController = TriengineSurfaceController();
  int? _selectedMaxFps; // 현재 선택된 max fps 상한. (null 값 == ADAPTIVE)
  
  @override
  void initState() {
    super.initState();
    _animationController = AnimationController(
      duration: _animationDuration,
      vsync: this, // TickerProviderStateMixin을 사용하여 vsync 활성화
    );
    _slideAnimation = Tween<double>(
      begin: 0.0, // 우측 패널이 완전히 닫힌 상태를 의미 (`_slideAnimation.value == 0.0`)
      end: 1.0, // 우측 패널이 완전히 열린 상태를 의미 (`_slideAnimation.value == 1.0`)
    ).animate(CurvedAnimation(
      parent: _animationController,
      curve: Curves.easeInOut,
    ));
  }
  
  @override
  void dispose() {
    _animationController.dispose();
    super.dispose();
  }
  
  void _togglePanelVisibility() {
    setState(() {
      _isRightPanelOpened = !_isRightPanelOpened;
      if (_isRightPanelOpened) {
        _animationController.forward();
      } else {
        _animationController.reverse();
      }
    });
  }
  
  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text("Source Renderer : '${widget.rendererIpcServerName}'"),
        titleTextStyle: const TextStyle(
          fontSize: 20,
        ),
        actions: [
          IconButton(
            icon: Icon(_isRightPanelOpened ? Icons.close : Icons.menu),
            onPressed: _togglePanelVisibility,
            tooltip: _isRightPanelOpened ? 'Close Control Panel' : 'Open Control Panel',
          ),
        ],
      ),
      body: AnimatedBuilder(
        animation: _slideAnimation,
        builder: (context, child) {
          final screenWidth = MediaQuery.of(context).size.width;

          // _slideAnimation.value가 연속적으로 부드럽게 변하면서 그에 맞춰 패널 너비를 함께 계산 (패널 슬라이드 효과 구현)
          final animatedRightPanelWidth = _currRightPanelWidth * _slideAnimation.value/* 0.0(패널 완전히 닫힘) ~ 1.0(패널 완전히 열림) */;
          final animatedLeftPanelWidth = screenWidth - animatedRightPanelWidth;
          
          return Stack(
            children: [
              // Left Panel - Main Scene (TriengineSurface)
              Positioned(
                left: 0,
                top: 0,
                width: animatedLeftPanelWidth,
                height: MediaQuery.of(context).size.height - kToolbarHeight,
                child: _buildSceneWidget(),
              ),
              
              // Right Panel - Main Scene Controller
              Positioned(
                // 우측 패널이 완전히 닫힌 경우 -> `animatedRightPanelWidth`가 0.0이 되므로 right distance 값은 `-_rightPanelWidth`이 됨
                // 우측 패널이 완전히 열린 경우 -> `animatedRightPanelWidth`가 `_rightPanelWidth`와 동일해지므로 right distance 값은 `0.0`이 됨
                right: -_currRightPanelWidth + animatedRightPanelWidth,
                top: 0,
                width: _currRightPanelWidth,
                height: MediaQuery.of(context).size.height - kToolbarHeight,
                child: _buildSceneControlPanelWidget(),
              ),
              
              // Splitter (우측 패널이 열려있을 때만 표시)
              if (_isRightPanelOpened)
                Positioned(
                  // 현재 우측 패널의 애니메이션된 너비 경계에 맞춰 splitter의 위치값(right distance 값)을 동적으로 조정(애니메이션 적용)
                  // 이 때 splitter 중앙 정렬을 위해 오프셋 값(`splitterWidth / 2`)을 추가로 빼준다.
                  right: animatedRightPanelWidth - (_splitterWidth / 2)/* 오프셋 값 */, 
                  top: 0,
                  width: _splitterWidth,
                  height: MediaQuery.of(context).size.height - kToolbarHeight,
                  child: _buildSplitterWidget(),
                ),
            ],
          );
        },
      ),
    );
  }

  Widget _buildSplitterWidget() {
    return MouseRegion(
      cursor: SystemMouseCursors.resizeColumn,
      onEnter: (_) => setState(() => _isSplitterMouseHovered = true),
      onExit: (_) => setState(() => _isSplitterMouseHovered = false),
      child: GestureDetector(
        onPanUpdate: (details) {
          setState(() {
            // 우측 패널 너비를 조절
            final currScreenWidth = MediaQuery.of(context).size.width;
            final currRightPanelMinWidth = currScreenWidth * _minRightPanelWidthRatio;
            final currRightPanelMaxWidth = currScreenWidth * _maxRightPanelWidthRatio;
            _currRightPanelWidth = (_currRightPanelWidth - details.delta.dx).clamp(
              currRightPanelMinWidth, 
              currRightPanelMaxWidth
            );
          });
        },
        child: AnimatedContainer(
          duration: _animationDuration,
          color: _isSplitterMouseHovered
            ? Color.fromARGB(255, 231, 129, 94)
            : Colors.transparent,
        ),
      ),
    );
  }

  Widget _buildSceneWidget() {
    return Container(
      margin: const EdgeInsets.all(16.0),
      child: LayoutBuilder(
        builder: (context, constraints) {
          final availWidth = constraints.maxWidth;
          final availHeight = constraints.maxHeight;
          return Center(
            child: ClipRRect( // for rounded corners
              borderRadius: BorderRadius.circular(16.0),
              child: TriengineSurface(
                rendererIpcServerName: widget.rendererIpcServerName,
                size: Size(availWidth, availHeight),
                devicePixelRatio: View.of(context).devicePixelRatio,
                controller: _surfaceController,
                initialMaxFps: _selectedMaxFps, // connect 시점 cap을 UI 선택값과 일치시킴
              ),
            ),
          );
        },
      ),
    );
  }

  Widget _buildSceneControlPanelWidget() {
    return Container(
      decoration: BoxDecoration(
        color: Colors.black,
      ),
      child: Column(
        children: [
          Container(
            padding: const EdgeInsets.all(16.0),
            decoration: BoxDecoration(
              color: Colors.black,
            ),
            child: const Row(
              children: [
                Text(
                  'Control Panel',
                  style: TextStyle(
                    color: Colors.white, 
                    fontSize: 18, 
                    fontWeight: FontWeight.bold
                  ),
                ),
              ],
            ),
          ),
          Expanded(
            child: Container(
              padding: const EdgeInsets.all(16.0),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  _buildMaxFpsControl(),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildMaxFpsControl() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Text(
          'Max FPS',
          style: TextStyle(
            color: Colors.white,
            fontSize: 14,
            fontWeight: FontWeight.w600,
          ),
        ),
        const SizedBox(height: 8),
        DropdownButton<int?>(
          value: _selectedMaxFps,
          isExpanded: true,
          dropdownColor: Colors.black87,
          style: const TextStyle(color: Colors.white),
          items: _maxFpsPresets
              .map((preset) => DropdownMenuItem<int?>(
                    value: preset.value,
                    child: Text(preset.label),
                  ))
              .toList(),
          onChanged: (value) {
            setState(() => _selectedMaxFps = value);
            _surfaceController.changeMaxFps(value);
          },
        ),
      ],
    );
  }
} // class