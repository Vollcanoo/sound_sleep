import 'package:flutter/material.dart';
import 'package:intl/date_symbol_data_local.dart';
import 'package:provider/provider.dart';
import 'app.dart';
import 'services/auth_service.dart';
import 'services/sleep_service.dart';
import 'services/device_service.dart';
import 'services/ble_data_service.dart';
import 'services/snore_api_service.dart';
import 'services/cloud_sync_service.dart';
import 'providers/auth_provider.dart';
import 'providers/sleep_provider.dart';
import 'providers/device_provider.dart';
import 'providers/realtime_provider.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await initializeDateFormatting('zh_CN');

  final authService = AuthService();
  await authService.init();

  runApp(AppBootstrap(authService: authService));
}

class AppBootstrap extends StatelessWidget {
  final AuthService authService;

  const AppBootstrap({super.key, required this.authService});

  @override
  Widget build(BuildContext context) {
    final sleepService = SleepService();
    final bleDataService = BleDataService();
    final deviceService = DeviceService(bleDataService);
    final snoreApiService = SnoreApiService();
    final cloudSyncService = CloudSyncService();

    sleepService.setCloudSync(cloudSyncService);

    return MultiProvider(
      providers: [
        ChangeNotifierProvider.value(value: bleDataService),
        ChangeNotifierProvider(create: (_) => AuthProvider(authService)),
        ChangeNotifierProvider(
          create: (_) => SleepProvider(sleepService, cloudSyncService),
        ),
        ChangeNotifierProvider(create: (_) => DeviceProvider(deviceService)),
        ChangeNotifierProvider(
          create: (_) => RealtimeProvider(
            bleService: bleDataService,
            snoreService: snoreApiService,
            sleepService: sleepService,
          ),
        ),
      ],
      child: const SleepApp(),
    );
  }
}
