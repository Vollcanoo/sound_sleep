import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';
import 'theme/app_theme.dart';
import 'screens/splash_screen.dart';
import 'screens/auth/login_screen.dart';
import 'screens/auth/register_screen.dart';
import 'screens/home/home_screen.dart';
import 'screens/report/report_detail_screen.dart';
import 'screens/device/device_scan_screen.dart';
import 'screens/profile/profile_edit_screen.dart';

class SleepApp extends StatelessWidget {
  const SleepApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp.router(
      title: '睡了吗',
      theme: AppTheme.lightTheme,
      debugShowCheckedModeBanner: false,
      routerConfig: _router(context),
    );
  }

  GoRouter _router(BuildContext context) {
    return GoRouter(
      initialLocation: '/splash',
      routes: [
        GoRoute(
          path: '/splash',
          builder: (context, state) => const SplashScreen(),
        ),
        GoRoute(
          path: '/login',
          builder: (context, state) => const LoginScreen(),
        ),
        GoRoute(
          path: '/register',
          builder: (context, state) => const RegisterScreen(),
        ),
        GoRoute(
          path: '/home',
          builder: (context, state) => const HomeScreen(),
        ),
        GoRoute(
          path: '/report/:id',
          builder: (context, state) => ReportDetailScreen(
            recordId: state.pathParameters['id']!,
          ),
        ),
        GoRoute(
          path: '/device/scan',
          builder: (context, state) => const DeviceScanScreen(),
        ),
        GoRoute(
          path: '/profile/edit',
          builder: (context, state) => const ProfileEditScreen(),
        ),
      ],
    );
  }
}
