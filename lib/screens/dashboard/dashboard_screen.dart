import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:intl/intl.dart';
import '../../providers/sleep_provider.dart';
import '../../providers/auth_provider.dart';
import '../../theme/app_theme.dart';
import '../../widgets/sleep_score_ring.dart';
import '../../widgets/stat_card.dart';
import '../../widgets/weekly_bar_chart.dart';

class DashboardScreen extends StatelessWidget {
  const DashboardScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final sleepProvider = context.watch<SleepProvider>();
    final authProvider = context.watch<AuthProvider>();
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
                                  DateFormat('yyyy年M月d日 EEEE', 'zh_CN')
                                      .format(DateTime.now()),
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
                            backgroundColor:
                                Colors.white.withValues(alpha: 0.2),
                            child:
                                const Icon(Icons.person, color: Colors.white),
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
          // ── 昨晚数据卡片 ──
          SliverPadding(
            padding: const EdgeInsets.fromLTRB(16, 20, 16, 0),
            sliver: SliverToBoxAdapter(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const Text(
                    '昨晚睡眠详情',
                    style: TextStyle(
                      fontSize: 17,
                      fontWeight: FontWeight.bold,
                    ),
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
                            value: DateFormat('HH:mm')
                                .format(latestRecord.bedTime),
                            iconColor: AppTheme.darkBlue,
                          ),
                        ),
                        const SizedBox(width: 8),
                        Expanded(
                          child: StatCard(
                            icon: Icons.wb_sunny_outlined,
                            label: '起床时间',
                            value: DateFormat('HH:mm')
                                .format(latestRecord.wakeTime),
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
                            value: '${latestRecord.snoringTotalMinutes}分钟',
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
                  ],
                  const SizedBox(height: 24),
                  const Text(
                    '本周睡眠时长',
                    style: TextStyle(
                      fontSize: 17,
                      fontWeight: FontWeight.bold,
                    ),
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
