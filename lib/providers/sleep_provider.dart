import 'package:flutter/foundation.dart';
import '../models/sleep_record.dart';
import '../services/sleep_service.dart';
import '../services/ai_analysis_service.dart';
import '../services/cloud_sync_service.dart';
import '../config/cloudbase_config.dart';

class SleepProvider extends ChangeNotifier {
  final SleepService _sleepService;
  final CloudSyncService _cloudSync;
  final AiAnalysisService _aiService = AiAnalysisService();

  final Set<String> _analyzingIds = {};
  bool _isSyncing = false;

  SleepProvider(this._sleepService, this._cloudSync) {
    _sleepService.addListener(_onRecordsChanged);
    fetchFromCloud();
  }

  void _onRecordsChanged() {
    notifyListeners();
  }

  @override
  void dispose() {
    _sleepService.removeListener(_onRecordsChanged);
    super.dispose();
  }

  List<SleepRecord> get records => _sleepService.getRecords();
  SleepRecord? get latestRecord => _sleepService.latestRecord;
  bool get isSyncing => _isSyncing;

  List<SleepRecord> getRecentRecords(int days) =>
      _sleepService.getRecentRecords(days);

  SleepRecord? getRecordById(String id) => _sleepService.getRecordById(id);

  SleepRecord? getRecordByDate(DateTime date) =>
      _sleepService.getRecordByDate(date);

  bool isAnalyzing(String recordId) => _analyzingIds.contains(recordId);

  Future<void> requestAiAnalysis(String recordId) async {
    final record = getRecordById(recordId);
    if (record == null) return;
    if (record.aiAnalysis != null) return;
    if (_analyzingIds.contains(recordId)) return;

    _analyzingIds.add(recordId);
    notifyListeners();

    try {
      final analysis = await _aiService.analyze(record);
      record.aiAnalysis = analysis;
      if (CloudBaseConfig.isConfigured) {
        _cloudSync.uploadSleepRecord(record).catchError((e) {
          debugPrint('[SleepProvider] AI分析结果上传失败: $e');
        });
      }
    } catch (e) {
      debugPrint('AI analysis failed: $e');
    }

    _analyzingIds.remove(recordId);
    notifyListeners();
  }

  /// 从云端拉取历史记录
  Future<void> fetchFromCloud() async {
    if (!CloudBaseConfig.isConfigured) return;
    _isSyncing = true;
    notifyListeners();

    try {
      final cloudRecords = await _cloudSync.fetchRecords();
      for (final record in cloudRecords) {
        _sleepService.addRecordLocal(record);
      }
      debugPrint('[SleepProvider] 从云端拉取了 ${cloudRecords.length} 条记录');
    } catch (e) {
      debugPrint('[SleepProvider] 云端拉取失败: $e');
    }

    _isSyncing = false;
    notifyListeners();
  }

  /// 上传一条记录到云端
  Future<void> uploadRecord(SleepRecord record) async {
    if (!CloudBaseConfig.isConfigured) return;
    try {
      await _cloudSync.uploadSleepRecord(record);
    } catch (e) {
      debugPrint('[SleepProvider] 上传失败: $e');
    }
  }

  /// 删除一条睡眠记录（本地 + 云端）
  Future<void> deleteRecord(String recordId) async {
    _sleepService.removeRecord(recordId);
    if (CloudBaseConfig.isConfigured) {
      try {
        await _cloudSync.deleteSleepRecord(recordId);
      } catch (e) {
        debugPrint('[SleepProvider] 云端删除失败: $e');
      }
    }
  }
}
