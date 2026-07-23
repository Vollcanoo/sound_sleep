import 'package:flutter/foundation.dart';
import '../models/sleep_record.dart';
import '../config/cloudbase_config.dart';
import 'cloud_sync_service.dart';

class SleepService extends ChangeNotifier {
  final List<SleepRecord> _records = [];
  CloudSyncService? _cloudSync;

  void setCloudSync(CloudSyncService sync) {
    _cloudSync = sync;
  }

  List<SleepRecord> getRecords() => List.unmodifiable(_records);

  SleepRecord? getRecordByDate(DateTime date) {
    try {
      return _records.firstWhere(
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
      return _records.firstWhere((r) => r.id == id);
    } catch (_) {
      return null;
    }
  }

  List<SleepRecord> getRecentRecords(int days) {
    final sorted = List<SleepRecord>.from(_records)
      ..sort((a, b) => b.bedTime.compareTo(a.bedTime));
    return sorted.take(days).toList();
  }

  SleepRecord? get latestRecord {
    if (_records.isEmpty) return null;
    final sorted = List<SleepRecord>.from(_records)
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
    if (_records.any((r) => r.id == record.id)) return;
    _records.add(record);
    notifyListeners();
  }
}
