import 'package:flutter/material.dart';
import 'package:intl/date_symbol_data_local.dart';
import 'package:provider/provider.dart';
import 'app.dart';
import 'services/auth_service.dart';
import 'services/sleep_service.dart';
import 'services/device_service.dart';
import 'providers/auth_provider.dart';
import 'providers/sleep_provider.dart';
import 'providers/device_provider.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await initializeDateFormatting('zh_CN');
  runApp(const AppBootstrap());
}

class AppBootstrap extends StatelessWidget {
  const AppBootstrap({super.key});

  @override
  Widget build(BuildContext context) {
    final authService = AuthService();
    final sleepService = SleepService();
    final deviceService = DeviceService();

    return MultiProvider(
      providers: [
        ChangeNotifierProvider(create: (_) => AuthProvider(authService)),
        ChangeNotifierProvider(create: (_) => SleepProvider(sleepService)),
        ChangeNotifierProvider(create: (_) => DeviceProvider(deviceService)),
      ],
      child: const SleepApp(),
    );
  }
}
