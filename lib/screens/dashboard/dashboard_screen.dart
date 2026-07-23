import 'dart:async';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:intl/intl.dart';
import '../../providers/sleep_provider.dart';
import '../../providers/auth_provider.dart';
import '../../providers/realtime_provider.dart';
import '../../theme/app_theme.dart';
import '../../widgets/sleep_score_ring.dart';
import '../../widgets/stat_card.dart';
import '../../widgets/weekly_bar_chart.dart';
import '../report/report_detail_screen.dart';

class DashboardScreen extends StatefulWidget {
  const DashboardScreen({super.key});

  @override
  State<DashboardScreen> createState() => _DashboardScreenState();
}

class _DashboardScreenState extends State<DashboardScreen> {
  Timer? _durationTimer;

  @override
  void initState() {
    super.initState();
    // 每秒刷新监测时长显示
    _durationTimer = Timer.periodic(const Duration(seconds: 1), (_) {
      if (mounted && context.read<RealtimeProvider>().isMonitoring) {
        setState(() {});
      }
    });
  }

  @override
  void dispose() {
    _durationTimer?.cancel();
    super.dispose();
  }

  String _formatDuration(Duration d) {
    final h = d.inHours;
    final m = d.inMinutes.remainder(60);
    final s = d.inSeconds.remainder(60);
    if (h > 0) return '$h时$m分$s秒';
    if (m > 0) return '$m分$s秒';
    return '$s秒';
  }

  @override
  Widget build(BuildContext context) {
    final sleepProvider = context.watch<SleepProvider>();
    final authProvider = context.watch<AuthProvider>();
    final realtimeProvider = context.watch<RealtimeProvider>();
    final latestRecord = sleepProvider.latestRecord;
    final recentRecords = sleepProvider.getRecentRecords(7);

    // 根据时间选择问候语
    final hour = DateTime.now().hour;
    final greeting = hour < 6
        ? '夜深了'
        : hour < 12
        ? '早上好'
        : hour < 18
        ? '下午好'
        : '晚上好';

    return Scaffold(
      body: CustomScrollView(
        slivers: [
          // ── 渐变头部区域 ──
          SliverToBoxAdapter(
            child: Container(
              decoration: const BoxDecoration(
                gradient: AppTheme.primaryGradient,
                borderRadius: BorderRadius.vertical(
                  bottom: Radius.circular(32),
                ),
              ),
              child: SafeArea(
                bottom: false,
                child: Padding(
                  padding: const EdgeInsets.fromLTRB(24, 16, 24, 32),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      // 问候
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          Expanded(
                            child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                Text(
                                  '$greeting，${authProvider.user?.nickname ?? '用户'}',
                                  style: const TextStyle(
                                    fontSize: 20,
                                    fontWeight: FontWeight.w600,
                                    color: Colors.white,
                                  ),
                                ),
                                const SizedBox(height: 4),
                                Text(
                                  DateFormat(
                                    'yyyy年M月d日 EEEE',
                                    'zh_CN',
                                  ).format(DateTime.now()),
                                  style: TextStyle(
                                    fontSize: 14,
                                    color: Colors.white.withValues(alpha: 0.8),
                                  ),
                                ),
                              ],
                            ),
                          ),
                          CircleAvatar(
                            radius: 22,
                            backgroundColor: Colors.white.withValues(
                              alpha: 0.2,
                            ),
                            child: const Icon(
                              Icons.person,
                              color: Colors.white,
                            ),
                          ),
                        ],
                      ),
                      const SizedBox(height: 28),
                      // 睡眠评分环
                      if (latestRecord != null)
                        Center(
                          child: Column(
                            children: [
                              const Text(
                                '昨晚睡眠评分',
                                style: TextStyle(
                                  fontSize: 15,
                                  color: Colors.white70,
                                ),
                              ),
                              const SizedBox(height: 16),
                              SleepScoreRing(
                                score: latestRecord.sleepScore,
                                size: 140,
                                ringColor: Colors.white,
                                backgroundColor: Colors.white24,
                              ),
                              const SizedBox(height: 12),
                              Text(
                                latestRecord.durationFormatted,
                                style: const TextStyle(
                                  fontSize: 18,
                                  fontWeight: FontWeight.w600,
                                  color: Colors.white,
                                ),
                              ),
                            ],
                          ),
                        ),
                    ],
                  ),
                ),
              ),
            ),
          ),
          // ── 实时监测卡片 ──
          SliverPadding(
            padding: const EdgeInsets.fromLTRB(16, 20, 16, 0),
            sliver: SliverToBoxAdapter(
              child: Card(
                clipBehavior: Clip.antiAlias,
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    // 渐变标题栏
                    Container(
                      width: double.infinity,
                      padding: const EdgeInsets.symmetric(
                        horizontal: 20,
                        vertical: 14,
                      ),
                      decoration: BoxDecoration(
                        gradient: realtimeProvider.isMonitoring
                            ? const LinearGradient(
                                colors: [Color(0xFF43A047), Color(0xFF66BB6A)],
                              )
                            : const LinearGradient(
                                colors: [
                                  AppTheme.primaryBlue,
                                  AppTheme.lightBlue,
                                ],
                              ),
                      ),
                      child: Row(
                        children: [
                          Icon(
                            realtimeProvider.isMonitoring
                                ? Icons.sensors
                                : Icons.sensors_off,
                            color: Colors.white,
                            size: 20,
                          ),
                          const SizedBox(width: 8),
                          const Text(
                            '实时监测',
                            style: TextStyle(
                              fontSize: 16,
                              fontWeight: FontWeight.w600,
                              color: Colors.white,
                            ),
                          ),
                          const Spacer(),
                          if (realtimeProvider.isMonitoring)
                            Container(
                              padding: const EdgeInsets.symmetric(
                                horizontal: 8,
                                vertical: 3,
                              ),
                              decoration: BoxDecoration(
                                color: Colors.white.withValues(alpha: 0.2),
                                borderRadius: BorderRadius.circular(12),
                              ),
                              child: Row(
                                mainAxisSize: MainAxisSize.min,
                                children: [
                                  Container(
                                    width: 6,
                                    height: 6,
                                    decoration: const BoxDecoration(
                                      color: Colors.white,
                                      shape: BoxShape.circle,
                                    ),
                                  ),
                                  const SizedBox(width: 4),
                                  const Text(
                                    '监测中',
                                    style: TextStyle(
                                      fontSize: 11,
                                      color: Colors.white,
                                      fontWeight: FontWeight.w500,
                                    ),
                                  ),
                                ],
                              ),
                            ),
                        ],
                      ),
                    ),

                    // 监测内容区域
                    Padding(
                      padding: const EdgeInsets.all(20),
                      child: realtimeProvider.isMonitoring
                          ? Column(
                              children: [
                                // 当前姿态 + 压力值
                                Row(
                                  children: [
                                    // 姿态图标 + 标签
                                    Expanded(
                                      child: Row(
                                        children: [
                                          Container(
                                            padding: const EdgeInsets.all(10),
                                            decoration: BoxDecoration(
                                              color: AppTheme.primaryBlue
                                                  .withValues(alpha: 0.1),
                                              borderRadius:
                                                  BorderRadius.circular(12),
                                            ),
                                            child: Icon(
                                              realtimeProvider.isOnBed
                                                  ? Icons.hotel
                                                  : Icons
                                                        .airline_seat_flat_angled,
                                              color: AppTheme.primaryBlue,
                                              size: 24,
                                            ),
                                          ),
                                          const SizedBox(width: 12),
                                          Column(
                                            crossAxisAlignment:
                                                CrossAxisAlignment.start,
                                            children: [
                                              Text(
                                                realtimeProvider
                                                    .currentPostureLabel,
                                                style: const TextStyle(
                                                  fontSize: 18,
                                                  fontWeight: FontWeight.bold,
                                                ),
                                              ),
                                              const SizedBox(height: 2),
                                              Text(
                                                realtimeProvider.isOnBed
                                                    ? '在床上'
                                                    : '不在床上',
                                                style: TextStyle(
                                                  fontSize: 13,
                                                  color:
                                                      realtimeProvider.isOnBed
                                                      ? const Color(0xFF43A047)
                                                      : Colors.grey.shade500,
                                                ),
                                              ),
                                            ],
                                          ),
                                        ],
                                      ),
                                    ),
                                    // 压力值
                                    Column(
                                      crossAxisAlignment:
                                          CrossAxisAlignment.end,
                                      children: [
                                        Text(
                                          realtimeProvider.currentPressure
                                              .toStringAsFixed(0),
                                          style: const TextStyle(
                                            fontSize: 24,
                                            fontWeight: FontWeight.bold,
                                            color: AppTheme.primaryBlue,
                                          ),
                                        ),
                                        Text(
                                          '压力值',
                                          style: TextStyle(
                                            fontSize: 12,
                                            color: Colors.grey.shade500,
                                          ),
                                        ),
                                      ],
                                    ),
                                  ],
                                ),
                                const SizedBox(height: 12),
                                if (realtimeProvider.currentSnoreReading !=
                                    null)
                                  Container(
                                    width: double.infinity,
                                    padding: const EdgeInsets.symmetric(
                                      vertical: 10,
                                      horizontal: 14,
                                    ),
                                    decoration: BoxDecoration(
                                      color:
                                          realtimeProvider
                                              .currentSnoreReading!
                                              .detected
                                          ? const Color(0xFFFFF3E0)
                                          : Colors.grey.shade100,
                                      borderRadius: BorderRadius.circular(10),
                                    ),
                                    child: Row(
                                      children: [
                                        Icon(
                                          Icons.graphic_eq,
                                          size: 18,
                                          color:
                                              realtimeProvider
                                                  .currentSnoreReading!
                                                  .detected
                                              ? const Color(0xFFE65100)
                                              : Colors.grey.shade600,
                                        ),
                                        const SizedBox(width: 8),
                                        Text(
                                          realtimeProvider
                                                  .currentSnoreReading!
                                                  .detected
                                              ? '检测到鼾声'
                                              : '未检测到鼾声',
                                        ),
                                        const Spacer(),
                                        Text(
                                          '概率 ${(realtimeProvider.currentSnoreReading!.probability * 100).toStringAsFixed(0)}%',
                                          style: const TextStyle(
                                            fontWeight: FontWeight.w600,
                                          ),
                                        ),
                                      ],
                                    ),
                                  ),
                                const SizedBox(height: 16),
                                // 监测时长
                                if (realtimeProvider.monitoringStart != null)
                                  Container(
                                    width: double.infinity,
                                    padding: const EdgeInsets.symmetric(
                                      vertical: 10,
                                      horizontal: 14,
                                    ),
                                    decoration: BoxDecoration(
                                      color: Colors.grey.shade100,
                                      borderRadius: BorderRadius.circular(10),
                                    ),
                                    child: Row(
                                      mainAxisAlignment:
                                          MainAxisAlignment.center,
                                      children: [
                                        Icon(
                                          Icons.timer_outlined,
                                          size: 16,
                                          color: Colors.grey.shade600,
                                        ),
                                        const SizedBox(width: 6),
                                        Text(
                                          '已监测 ${_formatDuration(DateTime.now().difference(realtimeProvider.monitoringStart!))}',
                                          style: TextStyle(
                                            fontSize: 14,
                                            color: Colors.grey.shade700,
                                            fontWeight: FontWeight.w500,
                                          ),
                                        ),
                                      ],
                                    ),
                                  ),
                                const SizedBox(height: 16),
                                // 停止按钮
                                SizedBox(
                                  width: double.infinity,
                                  child: ElevatedButton.icon(
                                    onPressed: () async {
                                      final userId =
                                          authProvider.user?.id ?? '';
                                      final record = await realtimeProvider
                                          .stopMonitoringAndGenerateReport(
                                            userId,
                                          );
                                      if (context.mounted && record != null) {
                                        Navigator.of(context).push(
                                          MaterialPageRoute(
                                            builder: (_) => ReportDetailScreen(
                                              recordId: record.id,
                                            ),
                                          ),
                                        );
                                      }
                                    },
                                    icon: const Icon(
                                      Icons.stop_circle_outlined,
                                      size: 20,
                                    ),
                                    label: const Text('停止监测'),
                                    style: ElevatedButton.styleFrom(
                                      backgroundColor: const Color(0xFFE53935),
                                      foregroundColor: Colors.white,
                                      minimumSize: const Size(0, 44),
                                      shape: RoundedRectangleBorder(
                                        borderRadius: BorderRadius.circular(12),
                                      ),
                                    ),
                                  ),
                                ),
                              ],
                            )
                          : Column(
                              children: [
                                Icon(
                                  Icons.nightlight_round,
                                  size: 40,
                                  color: AppTheme.primaryBlue.withValues(
                                    alpha: 0.3,
                                  ),
                                ),
                                const SizedBox(height: 8),
                                Text(
                                  '点击开始睡眠监测',
                                  style: TextStyle(
                                    fontSize: 14,
                                    color: Colors.grey.shade500,
                                  ),
                                ),
                                const SizedBox(height: 16),
                                SizedBox(
                                  width: double.infinity,
                                  child: ElevatedButton.icon(
                                    onPressed: () async {
                                      final started = await realtimeProvider
                                          .startMonitoring();
                                      if (!context.mounted || started) return;
                                      ScaffoldMessenger.of(
                                        context,
                                      ).showSnackBar(
                                        const SnackBar(
                                          content: Text(
                                            '请先在“我的设备”中连接 SleepMonitor',
                                          ),
                                        ),
                                      );
                                    },
                                    icon: const Icon(
                                      Icons.play_circle_outline,
                                      size: 20,
                                    ),
                                    label: const Text('开始监测'),
                                    style: ElevatedButton.styleFrom(
                                      minimumSize: const Size(0, 44),
                                      shape: RoundedRectangleBorder(
                                        borderRadius: BorderRadius.circular(12),
                                      ),
                                    ),
                                  ),
                                ),
                              ],
                            ),
                    ),
                  ],
                ),
              ),
            ),
          ),

          // ── 昨晚数据卡片 ──
          SliverPadding(
            padding: const EdgeInsets.fromLTRB(16, 16, 16, 0),
            sliver: SliverToBoxAdapter(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const Text(
                    '昨晚睡眠详情',
                    style: TextStyle(fontSize: 17, fontWeight: FontWeight.bold),
                  ),
                  const SizedBox(height: 12),
                  if (latestRecord != null) ...[
                    // 第一行：上床 / 起床
                    Row(
                      children: [
                        Expanded(
                          child: StatCard(
                            icon: Icons.bedtime,
                            label: '上床时间',
                            value: DateFormat(
                              'HH:mm',
                            ).format(latestRecord.bedTime),
                            iconColor: AppTheme.darkBlue,
                          ),
                        ),
                        const SizedBox(width: 8),
                        Expanded(
                          child: StatCard(
                            icon: Icons.wb_sunny_outlined,
                            label: '起床时间',
                            value: DateFormat(
                              'HH:mm',
                            ).format(latestRecord.wakeTime),
                            iconColor: AppTheme.lightBlue,
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 8),
                    // 第二行：打鼾 / 起身
                    Row(
                      children: [
                        Expanded(
                          child: StatCard(
                            icon: Icons.volume_up,
                            label: '打鼾时长',
                            value:
                                '${latestRecord.snoringTotalMinutes.toStringAsFixed(1)}分钟',
                            iconColor: const Color(0xFFFF9800),
                          ),
                        ),
                        const SizedBox(width: 8),
                        Expanded(
                          child: StatCard(
                            icon: Icons.directions_walk,
                            label: '夜间起身',
                            value: '${latestRecord.getUpCount}次',
                            iconColor: const Color(0xFF7C4DFF),
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 8),
                    // 第三行：主要睡姿
                    if (latestRecord.postureSegments.isNotEmpty)
                      Row(
                        children: [
                          Expanded(
                            child: StatCard(
                              icon: Icons.airline_seat_flat,
                              label: '主要睡姿',
                              value: latestRecord.dominantPostureLabel,
                              iconColor: const Color(0xFF1565C0),
                            ),
                          ),
                          const SizedBox(width: 8),
                          const Expanded(child: SizedBox()),
                        ],
                      ),
                  ],
                  const SizedBox(height: 24),
                  const Text(
                    '本周睡眠时长',
                    style: TextStyle(fontSize: 17, fontWeight: FontWeight.bold),
                  ),
                  const SizedBox(height: 12),
                  Card(
                    child: Padding(
                      padding: const EdgeInsets.all(16),
                      child: WeeklyBarChart(records: recentRecords),
                    ),
                  ),
                  const SizedBox(height: 24),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}
