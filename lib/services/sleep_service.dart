import 'package:flutter/foundation.dart';
import '../models/sleep_record.dart';
import '../config/cloudbase_config.dart';
import 'cloud_sync_service.dart';

class SleepService extends ChangeNotifier {
  final List<SleepRecord> _records = [];
  CloudSyncService? _cloudSync;
  String? _currentUserId;

  void setCloudSync(CloudSyncService sync) {
    _cloudSync = sync;
  }

  void setCurrentUser(String? userId) {
    _currentUserId = userId;
    notifyListeners();
  }

  String? get currentUserId => _currentUserId;

  List<SleepRecord> _userRecords() {
    if (_currentUserId == null) return _records;
    return _records.where((r) => r.userId == _currentUserId).toList();
  }

  List<SleepRecord> getRecords() => List.unmodifiable(_userRecords());

  SleepRecord? getRecordByDate(DateTime date) {
    try {
      return _userRecords().firstWhere(
        (r) =>
            r.date.year == date.year &&
            r.date.month == date.month &&
            r.date.day == date.day,
      );
    } catch (_) {
      return null;
    }
  }

  SleepRecord? getRecordById(String id) {
    try {
      return _userRecords().firstWhere((r) => r.id == id);
    } catch (_) {
      return null;
    }
  }

  List<SleepRecord> getRecentRecords(int days) {
    final filtered = _userRecords();
    final sorted = List<SleepRecord>.from(filtered)
      ..sort((a, b) => b.bedTime.compareTo(a.bedTime));
    return sorted.take(days).toList();
  }

  SleepRecord? get latestRecord {
    final filtered = _userRecords();
    if (filtered.isEmpty) return null;
    final sorted = List<SleepRecord>.from(filtered)
      ..sort((a, b) => b.bedTime.compareTo(a.bedTime));
    return sorted.first;
  }

  void addRecord(SleepRecord record) {
    _records.add(record);
    notifyListeners();
    if (CloudBaseConfig.isConfigured && _cloudSync != null) {
      _cloudSync!.uploadSleepRecord(record).catchError((e) {
        debugPrint('[SleepService] 后台上传失败: $e');
      });
    }
  }

  void addRecordLocal(SleepRecord record) {
    if (_records.any((r) =>
        r.id == record.id ||
        (r.bedTime == record.bedTime && r.wakeTime == record.wakeTime)
    )) {
      return;
    }
    _records.add(record);
    notifyListeners();
  }

  void removeRecord(String id) {
    _records.removeWhere((r) => r.id == id);
    notifyListeners();
  }
}
