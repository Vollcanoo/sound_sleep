import 'dart:async';
import 'dart:convert';
import 'dart:math';
import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import '../models/posture_event.dart';

/// BLE UART Service UUIDs（Nordic UART Service）
/// ESP32-S3 通过此服务串流姿态 CSV 数据
class BleUuids {
  static const String uartService = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
  static const String uartTx =
      '6e400003-b5a3-f393-e0a9-e50e24dcca9e'; // ESP32→手机（Notify）
  static const String uartRx =
      '6e400002-b5a3-f393-e0a9-e50e24dcca9e'; // 手机→ESP32（Write）
}

/// 管理与 ESP32-S3 睡眠监测设备的 BLE 连接，
/// 接收并解析实时姿态 CSV 数据流。
class BleDataService extends ChangeNotifier {
  BluetoothDevice? _connectedDevice;
  BluetoothCharacteristic? _txCharacteristic;
  BluetoothCharacteristic? _rxCharacteristic;
  StreamSubscription? _dataSubscription;
  StreamSubscription? _connectionStateSubscription;
  String _buffer = ''; // 不完整 CSV 行的缓冲区

  // 当前状态
  PostureReading? _latestReading;
  bool _isConnected = false;
  bool _isReceivingData = false;
  DateTime? _sessionStart;

  // 本次睡眠会话累积数据
  final List<PostureReading> _readings = [];

  // 实时数据流
  final _readingController = StreamController<PostureReading>.broadcast();
  final _connectionController = StreamController<bool>.broadcast();

  // Mock 相关
  Timer? _mockTimer;
  bool _isMockRunning = false;

  // ── Getters ──

  PostureReading? get latestReading => _latestReading;
  bool get isConnected => _isConnected;
  bool get isReceivingData => _isReceivingData;
  DateTime? get sessionStart => _sessionStart;
  List<PostureReading> get readings => List.unmodifiable(_readings);
  Stream<PostureReading> get readingStream => _readingController.stream;
  Stream<bool> get connectionStream => _connectionController.stream;

  // ── 连接与订阅 ──

  /// 连接到指定 ESP32 设备并订阅 UART 通知
  Future<void> connectAndSubscribe(BluetoothDevice device) async {
    try {
      // 连接设备
      await device.connect(timeout: const Duration(seconds: 10));
      _connectedDevice = device;

      // 监听连接状态变化
      _connectionStateSubscription =
          device.connectionState.listen((state) {
        final connected = state == BluetoothConnectionState.connected;
        if (_isConnected && !connected) {
          // 连接断开 → 尝试自动重连
          debugPrint('BLE 连接断开，尝试重连...');
          _isConnected = false;
          _isReceivingData = false;
          _connectionController.add(false);
          notifyListeners();
          _attemptReconnect(device);
        }
      });

      // 发现服务
      final services = await device.discoverServices();
      final uartService = services.firstWhere(
        (s) => s.uuid.toString().toLowerCase() == BleUuids.uartService,
        orElse: () => throw Exception('未找到 UART 服务'),
      );

      // 获取特征
      _txCharacteristic = uartService.characteristics.firstWhere(
        (c) => c.uuid.toString().toLowerCase() == BleUuids.uartTx,
        orElse: () => throw Exception('未找到 TX 特征'),
      );
      _rxCharacteristic = uartService.characteristics.firstWhere(
        (c) => c.uuid.toString().toLowerCase() == BleUuids.uartRx,
        orElse: () => throw Exception('未找到 RX 特征'),
      );

      // 启用 TX 通知
      await _txCharacteristic!.setNotifyValue(true);

      // 订阅数据流
      _dataSubscription =
          _txCharacteristic!.onValueReceived.listen(_onDataReceived);

      _isConnected = true;
      _connectionController.add(true);
      notifyListeners();

      debugPrint('BLE UART 连接成功: ${device.platformName}');
    } catch (e) {
      debugPrint('BLE 连接失败: $e');
      _isConnected = false;
      _connectionController.add(false);
      notifyListeners();
      rethrow;
    }
  }

  /// 处理收到的 BLE 数据（可能是不完整的 CSV 行）
  void _onDataReceived(List<int> data) {
    final chunk = utf8.decode(data, allowMalformed: true);
    _buffer += chunk;

    // 按换行符拆分，处理完整行
    while (_buffer.contains('\n')) {
      final newlineIndex = _buffer.indexOf('\n');
      final line = _buffer.substring(0, newlineIndex).trim();
      _buffer = _buffer.substring(newlineIndex + 1);

      if (line.isNotEmpty) {
        _processLine(line);
      }
    }
  }

  /// 解析一行完整的 CSV 数据
  void _processLine(String line) {
    try {
      final reading = PostureReading.fromCsv(line);

      _latestReading = reading;
      _readings.add(reading);

      if (!_isReceivingData) {
        _isReceivingData = true;
      }

      // 发送到数据流
      if (!_readingController.isClosed) {
        _readingController.add(reading);
      }
      notifyListeners();
    } on FormatException catch (e) {
      debugPrint('CSV 解析失败: $e — 原始行: $line');
    }
  }

  /// 向 ESP32 发送指令（如 'b' 触发重新校准）
  Future<void> sendCommand(String command) async {
    if (_rxCharacteristic == null || !_isConnected) {
      debugPrint('无法发送指令：未连接');
      return;
    }
    try {
      final bytes = utf8.encode(command);
      await _rxCharacteristic!.write(bytes, withoutResponse: true);
      debugPrint('已发送指令: $command');
    } catch (e) {
      debugPrint('发送指令失败: $e');
    }
  }

  /// 断开连接并清理资源
  Future<void> disconnect() async {
    await _dataSubscription?.cancel();
    _dataSubscription = null;

    await _connectionStateSubscription?.cancel();
    _connectionStateSubscription = null;

    if (_txCharacteristic != null) {
      try {
        await _txCharacteristic!.setNotifyValue(false);
      } catch (_) {}
    }

    if (_connectedDevice != null) {
      try {
        await _connectedDevice!.disconnect();
      } catch (_) {}
    }

    _connectedDevice = null;
    _txCharacteristic = null;
    _rxCharacteristic = null;
    _buffer = '';
    _isConnected = false;
    _isReceivingData = false;
    _connectionController.add(false);
    notifyListeners();

    debugPrint('BLE 已断开');
  }

  /// 自动重连（最多尝试 3 次，间隔递增）
  Future<void> _attemptReconnect(BluetoothDevice device) async {
    for (int attempt = 1; attempt <= 3; attempt++) {
      await Future.delayed(Duration(seconds: attempt * 2));
      debugPrint('重连尝试 $attempt/3...');
      try {
        await connectAndSubscribe(device);
        if (_isConnected) {
          debugPrint('重连成功');
          return;
        }
      } catch (e) {
        debugPrint('重连失败: $e');
      }
    }
    debugPrint('重连失败，已放弃');
  }

  // ── 会话管理 ──

  /// 开始新的监测会话（清除累积数据）
  void startSession() {
    _readings.clear();
    _latestReading = null;
    _sessionStart = DateTime.now();
    _isReceivingData = false;
    notifyListeners();
  }

  /// 结束监测会话，返回累积的读数
  List<PostureReading> endSession() {
    final result = List<PostureReading>.from(_readings);
    _sessionStart = null;
    notifyListeners();
    return result;
  }

  // ── Mock 模式（测试/演示用）──

  /// 启动模拟数据流，每秒生成一条假读数
  void startMockStream() {
    if (_isMockRunning) return;
    _isMockRunning = true;

    final random = Random(42); // 固定种子，保证可重复性
    const postures = [
      PostureType.supine,
      PostureType.leftSide,
      PostureType.rightSide,
      PostureType.moving,
    ];
    var currentPosture = PostureType.supine;
    var tickCount = 0;

    _isConnected = true;
    _connectionController.add(true);
    startSession();

    _mockTimer = Timer.periodic(const Duration(seconds: 1), (_) {
      tickCount++;

      // 每 15-30 秒随机切换一次姿态
      if (tickCount % (15 + random.nextInt(16)) == 0) {
        currentPosture = postures[random.nextInt(postures.length)];
      }

      // 根据当前姿态生成合理的传感器数据
      final int rawLeft;
      final int rawCenter;
      final int rawRight;

      switch (currentPosture) {
        case PostureType.leftSide:
          rawLeft = 800 + random.nextInt(200);
          rawCenter = 200 + random.nextInt(150);
          rawRight = 50 + random.nextInt(80);
        case PostureType.rightSide:
          rawLeft = 50 + random.nextInt(80);
          rawCenter = 200 + random.nextInt(150);
          rawRight = 800 + random.nextInt(200);
        case PostureType.supine:
          rawLeft = 300 + random.nextInt(100);
          rawCenter = 600 + random.nextInt(200);
          rawRight = 300 + random.nextInt(100);
        case PostureType.moving:
          rawLeft = random.nextInt(600);
          rawCenter = random.nextInt(600);
          rawRight = random.nextInt(600);
        default:
          rawLeft = 100 + random.nextInt(100);
          rawCenter = 100 + random.nextInt(100);
          rawRight = 100 + random.nextInt(100);
      }

      final total = (rawLeft + rawCenter + rawRight).toDouble();
      final leftR = total > 0 ? rawLeft / total : 0.0;
      final centerR = total > 0 ? rawCenter / total : 0.0;
      final rightR = total > 0 ? rawRight / total : 0.0;

      final isMoving = currentPosture == PostureType.moving;
      final confidence = isMoving
          ? 0.5 + random.nextDouble() * 0.3
          : 0.8 + random.nextDouble() * 0.2;

      // 组装 CSV 行并通过 _processLine 解析
      final csv = [
        rawLeft,
        rawCenter,
        rawRight,
        rawLeft.toDouble().toStringAsFixed(1),
        rawCenter.toDouble().toStringAsFixed(1),
        rawRight.toDouble().toStringAsFixed(1),
        total.toStringAsFixed(1),
        leftR.toStringAsFixed(4),
        centerR.toStringAsFixed(4),
        rightR.toStringAsFixed(4),
        (leftR * 10 - 5).toStringAsFixed(2), // xCenterCm
        isMoving ? '1' : '0',
        currentPosture.name.toUpperCase().replaceAllMapped(
              RegExp(r'([a-z])([A-Z])'),
              (m) => '${m[1]}_${m[2]}',
            ),
        confidence.toStringAsFixed(4),
      ].join(',');

      _processLine(csv);
    });

    notifyListeners();
  }

  /// 停止模拟数据流
  void stopMockStream() {
    _mockTimer?.cancel();
    _mockTimer = null;
    _isMockRunning = false;
    _isConnected = false;
    _isReceivingData = false;
    _connectionController.add(false);
    notifyListeners();
  }

  @override
  void dispose() {
    stopMockStream();
    disconnect();
    _readingController.close();
    _connectionController.close();
    super.dispose();
  }
}
