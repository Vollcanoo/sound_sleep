import 'dart:async';
import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import '../models/posture_event.dart';
import '../models/realtime_snore_reading.dart';

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
  RealtimeSnoreReading? _latestSnoreReading;
  bool _isConnected = false;
  bool _isWifiConnected = false;
  bool _isReceivingData = false;
  DateTime? _sessionStart;

  // 本次睡眠会话累积数据
  final List<PostureReading> _readings = [];

  // 实时数据流
  final _readingController = StreamController<PostureReading>.broadcast();
  final _snoreReadingController =
      StreamController<RealtimeSnoreReading>.broadcast();
  final _connectionController = StreamController<bool>.broadcast();
  final _rawResponseController = StreamController<List<int>>.broadcast();

  // ── Getters ──

  PostureReading? get latestReading => _latestReading;
  RealtimeSnoreReading? get latestSnoreReading => _latestSnoreReading;
  bool get isConnected => _isConnected;
  bool get isWifiConnected => _isWifiConnected;
  bool get isReceivingData => _isReceivingData;
  DateTime? get sessionStart => _sessionStart;
  List<PostureReading> get readings => List.unmodifiable(_readings);
  Stream<PostureReading> get readingStream => _readingController.stream;
  Stream<RealtimeSnoreReading> get snoreReadingStream =>
      _snoreReadingController.stream;
  Stream<bool> get connectionStream => _connectionController.stream;
  Stream<List<int>> get rawResponseStream => _rawResponseController.stream;

  // ── 连接与订阅 ──

  /// 连接到指定 ESP32 设备并订阅 UART 通知
  Future<void> connectAndSubscribe(BluetoothDevice device) async {
    try {
      // 连接设备
      await device.connect(timeout: const Duration(seconds: 10));
      _connectedDevice = device;

      // 取消旧的订阅，避免重连时 listener 堆积
      await _connectionStateSubscription?.cancel();
      await _dataSubscription?.cancel();

      // 监听连接状态变化
      _connectionStateSubscription = device.connectionState.listen((state) {
        final connected = state == BluetoothConnectionState.connected;
        if (_isConnected && !connected) {
          // 连接断开 → 尝试自动重连
          debugPrint('BLE 连接断开，尝试重连...');
          _isConnected = false;
          _isWifiConnected = false;
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
      _dataSubscription = _txCharacteristic!.onValueReceived.listen(
        _onDataReceived,
      );

      _isConnected = true;
      _connectionController.add(true);
      notifyListeners();

      await sendCommand('device_status');

      debugPrint('BLE UART 连接成功: ${device.platformName}');
    } catch (e) {
      debugPrint('BLE 连接失败: $e');
      _isConnected = false;
      _isWifiConnected = false;
      _connectionController.add(false);
      notifyListeners();
      rethrow;
    }
  }

  /// 处理收到的 BLE 数据（可能是不完整的 CSV 行）
  void _onDataReceived(List<int> data) {
    if (!_rawResponseController.isClosed) {
      _rawResponseController.add(data);
    }

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

  /// Parses one newline-delimited BLE message. Posture readings use CSV while
  /// model inference results and command acknowledgements use JSON.
  void _processLine(String line) {
    if (line.startsWith('{')) {
      _processJsonLine(line);
      return;
    }

    try {
      final reading = PostureReading.fromCsv(line);

      _latestReading = reading;
      _readings.add(reading);
      if (_readings.length > 43200) {
        _readings.removeAt(0);
      }

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

  void _processJsonLine(String line) {
    try {
      final decoded = jsonDecode(line);
      if (decoded is! Map<String, dynamic>) {
        return;
      }

      if (decoded['type'] == 'device_status') {
        _isWifiConnected = decoded['wifi_connected'] == true;
        notifyListeners();
        return;
      }

      if (decoded['status'] == 'ok' && decoded['msg'] == 'wifi_connected') {
        _isWifiConnected = true;
        notifyListeners();
        return;
      }

      if ((decoded['status'] == 'ok' && decoded['msg'] == 'wifi_cleared') ||
          (decoded['status'] == 'error' && decoded['msg'] == 'connect_failed')) {
        _isWifiConnected = false;
        notifyListeners();
        return;
      }

      if (decoded['type'] != 'snore') return;

      final reading = RealtimeSnoreReading.fromJson(decoded);
      _latestSnoreReading = reading;
      if (!_snoreReadingController.isClosed) {
        _snoreReadingController.add(reading);
      }
      notifyListeners();
    } on FormatException catch (e) {
      debugPrint('BLE JSON parse failed: $e');
    }
  }

  /// 向 ESP32 发送指令（如 'b' 触发重新校准）
  Future<bool> sendCommand(String command) async {
    if (_rxCharacteristic == null || !_isConnected) {
      debugPrint('无法发送指令：未连接');
      return false;
    }
    try {
      final bytes = utf8.encode(command);
      // Monitoring commands are state changes. Waiting for the GATT response
      // guarantees the ESP32 handled the write before a later disconnect.
      await _rxCharacteristic!.write(bytes, withoutResponse: false);
      debugPrint('已发送指令: $command');
      return true;
    } catch (e) {
      debugPrint('发送指令失败: $e');
      return false;
    }
  }

  /// 向 ESP32 发送原始字节（用于 WiFi 配网 JSON）
  Future<bool> sendRawBytes(List<int> bytes) async {
    if (_rxCharacteristic == null || !_isConnected) return false;
    try {
      await _rxCharacteristic!.write(bytes, withoutResponse: false);
      return true;
    } catch (e) {
      debugPrint('sendRawBytes 失败: $e');
      return false;
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
    _latestSnoreReading = null;
    _isConnected = false;
    _isWifiConnected = false;
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
    _latestSnoreReading = null;
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

  @override
  void dispose() {
    disconnect();
    _readingController.close();
    _snoreReadingController.close();
    _connectionController.close();
    _rawResponseController.close();
    super.dispose();
  }
}
