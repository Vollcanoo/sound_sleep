import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:intl/intl.dart';
import '../../providers/sleep_provider.dart';
import '../../theme/app_theme.dart';
import '../../models/sleep_record.dart';
import '../../widgets/sleep_score_ring.dart';
import '../../widgets/sleep_stage_chart.dart';
import '../../widgets/snoring_chart.dart';
import '../../widgets/posture_chart.dart';

class ReportDetailScreen extends StatefulWidget {
  final String recordId;

  const ReportDetailScreen({super.key, required this.recordId});

  @override
  State<ReportDetailScreen> createState() => _ReportDetailScreenState();
}

class _ReportDetailScreenState extends State<ReportDetailScreen> {
  @override
  void initState() {
    super.initState();
    // 报告打开时自动触发 AI 分析
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted) {
        context.read<SleepProvider>().requestAiAnalysis(widget.recordId);
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    final sleepProvider = context.watch<SleepProvider>();
    final record = sleepProvider.getRecordById(widget.recordId);

    if (record == null) {
      return Scaffold(
        appBar: AppBar(title: const Text('睡眠报告')),
        body: const Center(child: Text('未找到记录')),
      );
    }

    final isAnalyzing = sleepProvider.isAnalyzing(widget.recordId);
    final aiAnalysis = record.aiAnalysis;

    return Scaffold(
      appBar: AppBar(
        title: Text(DateFormat('M月d日 EEEE', 'zh_CN').format(record.date)),
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // ── 评分卡片 ──
            Card(
              child: Padding(
                padding: const EdgeInsets.symmetric(
                  vertical: 28,
                  horizontal: 24,
                ),
                child: Row(
                  children: [
                    SleepScoreRing(score: record.sleepScore, size: 120),
                    const SizedBox(width: 24),
                    Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(
                            record.sleepQualityLabel,
                            style: const TextStyle(
                              fontSize: 22,
                              fontWeight: FontWeight.bold,
                              color: AppTheme.darkBlue,
                            ),
                          ),
                          const SizedBox(height: 8),
                          _MiniStat(
                            icon: Icons.access_time,
                            label: '总时长',
                            value: record.durationFormatted,
                          ),
                          const SizedBox(height: 6),
                          _MiniStat(
                            icon: Icons.hotel,
                            label: '实际睡眠',
                            value: record.actualSleepFormatted,
                          ),
                          const SizedBox(height: 6),
                          _MiniStat(
                            icon: Icons.directions_walk,
                            label: '夜间起身',
                            value: '${record.getUpCount}次',
                          ),
                        ],
                      ),
                    ),
                  ],
                ),
              ),
            ),
            const SizedBox(height: 16),

            // ── AI 智能分析 ──
            _buildAiSection(isAnalyzing, aiAnalysis),
            const SizedBox(height: 16),

            // ── 睡眠时段 ──
            Card(
              child: Padding(
                padding: const EdgeInsets.all(20),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const _SectionTitle(icon: Icons.timeline, title: '睡眠时段'),
                    const SizedBox(height: 16),
                    SleepStageChart(
                      bedTime: record.bedTime,
                      wakeTime: record.wakeTime,
                      pressureSegments: record.pressureSegments,
                      getUpCount: record.getUpCount,
                    ),
                  ],
                ),
              ),
            ),
            const SizedBox(height: 16),

            // ── 睡姿分布 ──
            _buildPostureSection(record),
            const SizedBox(height: 16),

            // ── 数据统计 2x2 ──
            Row(
              children: [
                Expanded(
                  child: _DataCard(
                    icon: Icons.bedtime,
                    label: '上床时间',
                    value: DateFormat('HH:mm').format(record.bedTime),
                    color: AppTheme.darkBlue,
                  ),
                ),
                const SizedBox(width: 10),
                Expanded(
                  child: _DataCard(
                    icon: Icons.wb_sunny_outlined,
                    label: '起床时间',
                    value: DateFormat('HH:mm').format(record.wakeTime),
                    color: AppTheme.lightBlue,
                  ),
                ),
              ],
            ),
            const SizedBox(height: 10),
            Row(
              children: [
                Expanded(
                  child: _DataCard(
                    icon: Icons.directions_walk,
                    label: '起身次数',
                    value: '${record.getUpCount}次',
                    color: const Color(0xFF7C4DFF),
                    subtitle: record.getUpCount > 0
                        ? '离床${record.awayMinutes}分钟'
                        : '整夜未起身',
                  ),
                ),
                const SizedBox(width: 10),
                Expanded(
                  child: _DataCard(
                    icon: Icons.volume_up,
                    label: '打鼾次数',
                    value: '${record.snoringEvents.length}次',
                    color: const Color(0xFFFF9800),
                    subtitle:
                        '共${record.snoringTotalMinutes.toStringAsFixed(1)}分钟',
                  ),
                ),
              ],
            ),
            const SizedBox(height: 16),

            // ── 打鼾图表 ──
            Card(
              child: Padding(
                padding: const EdgeInsets.all(20),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(
                      children: [
                        const _SectionTitle(
                          icon: Icons.graphic_eq,
                          title: '打鼾检测',
                        ),
                        const Spacer(),
                        if (record.snoringEvents.isNotEmpty &&
                            record.snoringMaxDecibel > 0)
                          Container(
                            padding: const EdgeInsets.symmetric(
                              horizontal: 8,
                              vertical: 4,
                            ),
                            decoration: BoxDecoration(
                              color: const Color(
                                0xFFFF9800,
                              ).withValues(alpha: 0.1),
                              borderRadius: BorderRadius.circular(8),
                            ),
                            child: Text(
                              '最高 ${record.snoringMaxDecibel.toStringAsFixed(0)} dB',
                              style: const TextStyle(
                                fontSize: 12,
                                color: Color(0xFFFF9800),
                                fontWeight: FontWeight.w500,
                              ),
                            ),
                          ),
                      ],
                    ),
                    const SizedBox(height: 16),
                    SnoringChart(events: record.snoringEvents),
                  ],
                ),
              ),
            ),

            // ── 报告说明 ──
            const SizedBox(height: 16),
            Container(
              padding: const EdgeInsets.all(14),
              decoration: BoxDecoration(
                color: Colors.grey.shade100,
                borderRadius: BorderRadius.circular(12),
              ),
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Icon(Icons.sensors, size: 18, color: Colors.grey.shade500),
                  const SizedBox(width: 10),
                  Expanded(
                    child: Text(
                      '本报告由压力传感器自动生成。从首次检测到压力（上床）到压力最终消失（离床），'
                      '期间短暂的压力消失计为起身事件。',
                      style: TextStyle(
                        fontSize: 12,
                        color: Colors.grey.shade500,
                        height: 1.5,
                      ),
                    ),
                  ),
                ],
              ),
            ),
            const SizedBox(height: 24),
          ],
        ),
      ),
    );
  }

  /// ── AI 智能分析区域 ──
  Widget _buildAiSection(bool isAnalyzing, AiAnalysis? aiAnalysis) {
    return Card(
      clipBehavior: Clip.antiAlias,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // 渐变标题栏
          Container(
            width: double.infinity,
            padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 14),
            decoration: const BoxDecoration(
              gradient: LinearGradient(
                colors: [Color(0xFF1565C0), Color(0xFF7C4DFF)],
              ),
            ),
            child: Row(
              children: [
                const Icon(Icons.auto_awesome, color: Colors.white, size: 20),
                const SizedBox(width: 8),
                const Text(
                  'AI 智能分析',
                  style: TextStyle(
                    fontSize: 16,
                    fontWeight: FontWeight.w600,
                    color: Colors.white,
                  ),
                ),
                const Spacer(),
                if (aiAnalysis != null)
                  Text(
                    aiAnalysis.model ?? '',
                    style: TextStyle(
                      fontSize: 10,
                      color: Colors.white.withValues(alpha: 0.7),
                    ),
                  ),
              ],
            ),
          ),

          // 内容区
          Padding(
            padding: const EdgeInsets.all(20),
            child: isAnalyzing
                ? _buildAnalyzingState()
                : aiAnalysis != null
                ? _buildAnalysisResult(aiAnalysis)
                : _buildNoAnalysis(),
          ),
        ],
      ),
    );
  }

  /// 分析中的加载状态
  Widget _buildAnalyzingState() {
    return Column(
      children: [
        const SizedBox(height: 8),
        Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            SizedBox(
              width: 20,
              height: 20,
              child: CircularProgressIndicator(
                strokeWidth: 2,
                color: AppTheme.primaryBlue.withValues(alpha: 0.6),
              ),
            ),
            const SizedBox(width: 12),
            Text(
              '正在分析你的睡眠数据...',
              style: TextStyle(fontSize: 14, color: Colors.grey.shade600),
            ),
          ],
        ),
        const SizedBox(height: 8),
      ],
    );
  }

  /// 分析结果展示
  Widget _buildAnalysisResult(AiAnalysis aiAnalysis) {
    final isLocalRules =
        aiAnalysis.model != null && aiAnalysis.model!.contains('本地规则');

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        if (isLocalRules) ...[
          Container(
            width: double.infinity,
            padding: const EdgeInsets.all(12),
            decoration: BoxDecoration(
              color: Colors.orange.shade50,
              borderRadius: BorderRadius.circular(10),
              border: Border.all(color: Colors.orange.shade200),
            ),
            child: Row(
              children: [
                Icon(
                  Icons.warning_amber_rounded,
                  size: 20,
                  color: Colors.orange.shade700,
                ),
                const SizedBox(width: 8),
                Expanded(
                  child: Text(
                    '未配置 API 密钥，当前使用本地规则分析。'
                    '如需 AI 深度分析，请配置 VolcEngine API 密钥。',
                    style: TextStyle(
                      fontSize: 12,
                      color: Colors.orange.shade800,
                      height: 1.4,
                    ),
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 12),
        ],
        // 一句话总结
        Container(
          padding: const EdgeInsets.all(12),
          decoration: BoxDecoration(
            color: AppTheme.primaryBlue.withValues(alpha: 0.05),
            borderRadius: BorderRadius.circular(10),
          ),
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const Icon(
                Icons.summarize,
                size: 18,
                color: AppTheme.primaryBlue,
              ),
              const SizedBox(width: 8),
              Expanded(
                child: Text(
                  aiAnalysis.summary,
                  style: const TextStyle(
                    fontSize: 14,
                    fontWeight: FontWeight.w500,
                    height: 1.5,
                  ),
                ),
              ),
            ],
          ),
        ),
        const SizedBox(height: 16),

        // 分析要点
        if (aiAnalysis.insights.isNotEmpty) ...[
          const Text(
            '📊 分析详情',
            style: TextStyle(
              fontSize: 14,
              fontWeight: FontWeight.w600,
              color: AppTheme.darkBlue,
            ),
          ),
          const SizedBox(height: 8),
          ...aiAnalysis.insights.map<Widget>(
            (insight) => Padding(
              padding: const EdgeInsets.only(bottom: 8),
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Container(
                    margin: const EdgeInsets.only(top: 6),
                    width: 6,
                    height: 6,
                    decoration: const BoxDecoration(
                      color: AppTheme.primaryBlue,
                      shape: BoxShape.circle,
                    ),
                  ),
                  const SizedBox(width: 10),
                  Expanded(
                    child: Text(
                      insight,
                      style: TextStyle(
                        fontSize: 13,
                        color: Colors.grey.shade700,
                        height: 1.5,
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
          const SizedBox(height: 12),
        ],

        // 改善建议
        if (aiAnalysis.suggestions.isNotEmpty) ...[
          const Text(
            '💡 改善建议',
            style: TextStyle(
              fontSize: 14,
              fontWeight: FontWeight.w600,
              color: Color(0xFF7C4DFF),
            ),
          ),
          const SizedBox(height: 8),
          ...aiAnalysis.suggestions.asMap().entries.map<Widget>((entry) {
            return Container(
              margin: const EdgeInsets.only(bottom: 8),
              padding: const EdgeInsets.all(10),
              decoration: BoxDecoration(
                color: const Color(0xFF7C4DFF).withValues(alpha: 0.04),
                borderRadius: BorderRadius.circular(8),
                border: Border.all(
                  color: const Color(0xFF7C4DFF).withValues(alpha: 0.1),
                ),
              ),
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    '${entry.key + 1}',
                    style: const TextStyle(
                      fontSize: 12,
                      fontWeight: FontWeight.bold,
                      color: Color(0xFF7C4DFF),
                    ),
                  ),
                  const SizedBox(width: 8),
                  Expanded(
                    child: Text(
                      entry.value,
                      style: TextStyle(
                        fontSize: 13,
                        color: Colors.grey.shade700,
                        height: 1.4,
                      ),
                    ),
                  ),
                ],
              ),
            );
          }),
        ],
      ],
    );
  }

  /// 无分析结果 + 手动触发按钮
  Widget _buildNoAnalysis() {
    return Column(
      children: [
        const SizedBox(height: 4),
        Icon(Icons.psychology, size: 36, color: Colors.grey.shade300),
        const SizedBox(height: 8),
        Text(
          '暂无 AI 分析',
          style: TextStyle(fontSize: 14, color: Colors.grey.shade500),
        ),
        const SizedBox(height: 4),
      ],
    );
  }

  /// ── 睡姿分布区域 ──
  Widget _buildPostureSection(SleepRecord record) {
    final hasPostureData = record.postureSegments.isNotEmpty;

    return Card(
      clipBehavior: Clip.antiAlias,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Padding(
            padding: const EdgeInsets.fromLTRB(20, 20, 20, 0),
            child: Row(
              children: [
                const _SectionTitle(
                  icon: Icons.airline_seat_flat,
                  title: '睡姿分布',
                ),
                const Spacer(),
                if (hasPostureData)
                  Container(
                    padding: const EdgeInsets.symmetric(
                      horizontal: 8,
                      vertical: 4,
                    ),
                    decoration: BoxDecoration(
                      color: AppTheme.primaryBlue.withValues(alpha: 0.1),
                      borderRadius: BorderRadius.circular(8),
                    ),
                    child: Text(
                      '主要睡姿：${record.dominantPostureLabel}',
                      style: const TextStyle(
                        fontSize: 12,
                        color: AppTheme.primaryBlue,
                        fontWeight: FontWeight.w500,
                      ),
                    ),
                  ),
              ],
            ),
          ),
          Padding(
            padding: const EdgeInsets.all(20),
            child: PostureChart(
              distribution: record.postureDistribution,
              totalMinutes: record.postureSegments.fold(
                0,
                (sum, seg) => sum + seg.durationMinutes,
              ),
            ),
          ),
        ],
      ),
    );
  }
}

// ── 子组件 ──

class _SectionTitle extends StatelessWidget {
  final IconData icon;
  final String title;

  const _SectionTitle({required this.icon, required this.title});

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Icon(icon, size: 18, color: AppTheme.primaryBlue),
        const SizedBox(width: 6),
        Text(
          title,
          style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w600),
        ),
      ],
    );
  }
}

class _MiniStat extends StatelessWidget {
  final IconData icon;
  final String label;
  final String value;

  const _MiniStat({
    required this.icon,
    required this.label,
    required this.value,
  });

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Icon(icon, size: 14, color: Colors.grey.shade500),
        const SizedBox(width: 6),
        Text(
          '$label ',
          style: TextStyle(fontSize: 13, color: Colors.grey.shade600),
        ),
        Text(
          value,
          style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w600),
        ),
      ],
    );
  }
}

class _DataCard extends StatelessWidget {
  final IconData icon;
  final String label;
  final String value;
  final Color color;
  final String? subtitle;

  const _DataCard({
    required this.icon,
    required this.label,
    required this.value,
    required this.color,
    this.subtitle,
  });

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Container(
                  padding: const EdgeInsets.all(6),
                  decoration: BoxDecoration(
                    color: color.withValues(alpha: 0.1),
                    borderRadius: BorderRadius.circular(8),
                  ),
                  child: Icon(icon, color: color, size: 18),
                ),
                const SizedBox(width: 8),
                Text(
                  label,
                  style: TextStyle(fontSize: 13, color: Colors.grey.shade600),
                ),
              ],
            ),
            const SizedBox(height: 10),
            Text(
              value,
              style: const TextStyle(fontSize: 22, fontWeight: FontWeight.bold),
            ),
            if (subtitle != null) ...[
              const SizedBox(height: 4),
              Text(
                subtitle!,
                style: TextStyle(fontSize: 11, color: Colors.grey.shade500),
              ),
            ],
          ],
        ),
      ),
    );
  }
}
