import 'package:flutter/foundation.dart';
import '../models/sleep_record.dart';
import '../services/sleep_service.dart';
import '../services/ai_analysis_service.dart';

class SleepProvider extends ChangeNotifier {
  final SleepService _sleepService;
  final AiAnalysisService _aiService = AiAnalysisService();

  /// 正在分析中的记录 ID 集合
  final Set<String> _analyzingIds = {};

  SleepProvider(this._sleepService);

  List<SleepRecord> get records => _sleepService.getRecords();

  SleepRecord? get latestRecord => _sleepService.latestRecord;

  List<SleepRecord> getRecentRecords(int days) =>
      _sleepService.getRecentRecords(days);

  SleepRecord? getRecordById(String id) => _sleepService.getRecordById(id);

  SleepRecord? getRecordByDate(DateTime date) =>
      _sleepService.getRecordByDate(date);

  /// 该记录是否正在 AI 分析中
  bool isAnalyzing(String recordId) => _analyzingIds.contains(recordId);

  /// 请求 AI 分析某条记录
  /// 报告自动生成后可立即调用，或用户手动点击触发
  Future<void> requestAiAnalysis(String recordId) async {
    final record = getRecordById(recordId);
    if (record == null) return;
    if (record.aiAnalysis != null) return; // 已有分析结果
    if (_analyzingIds.contains(recordId)) return; // 正在分析中

    _analyzingIds.add(recordId);
    notifyListeners();

    try {
      final analysis = await _aiService.analyze(record);
      record.aiAnalysis = analysis;
    } catch (e) {
      debugPrint('AI analysis failed: $e');
    }

    _analyzingIds.remove(recordId);
    notifyListeners();
  }
}
