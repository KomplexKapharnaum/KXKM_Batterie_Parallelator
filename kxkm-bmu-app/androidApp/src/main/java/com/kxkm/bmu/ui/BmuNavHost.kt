package com.kxkm.bmu.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bolt
import androidx.compose.material.icons.filled.Favorite
import androidx.compose.material.icons.filled.Hub
import androidx.compose.material.icons.filled.ListAlt
import androidx.compose.material.icons.filled.Memory
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.NavType
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import androidx.navigation.navArgument
import com.kxkm.bmu.ui.audit.AuditScreen
import com.kxkm.bmu.ui.components.StatusBar
import com.kxkm.bmu.ui.config.ConfigScreen
import com.kxkm.bmu.ui.config.ProtectionConfigScreen
import com.kxkm.bmu.ui.config.SyncConfigScreen
import com.kxkm.bmu.ui.config.TransportConfigScreen
import com.kxkm.bmu.ui.config.UserManagementScreen
import com.kxkm.bmu.ui.config.WifiConfigScreen
import com.kxkm.bmu.ui.dashboard.DashboardScreen
import com.kxkm.bmu.ui.detail.BatteryDetailScreen
import com.kxkm.bmu.ui.fleet.FleetScreen
import com.kxkm.bmu.ui.soh.SohDashboardScreen
import com.kxkm.bmu.ui.system.SystemScreen
import com.kxkm.bmu.util.canConfigure
import com.kxkm.bmu.viewmodel.AuthViewModel

sealed class BmuRoute(val route: String, val label: String, val icon: ImageVector) {
    data object Dashboard : BmuRoute("dashboard", "Batteries", Icons.Filled.Bolt)
    data object System : BmuRoute("system", "Syst\u00e8me", Icons.Filled.Memory)
    data object Audit : BmuRoute("audit", "Audit", Icons.Filled.ListAlt)
    data object Soh : BmuRoute("soh", "SOH", Icons.Filled.Favorite)
    data object Fleet : BmuRoute("fleet", "Flotte", Icons.Filled.Hub)
    data object Config : BmuRoute("config", "Config", Icons.Filled.Settings)
}

@Composable
fun BmuNavHost(authVM: AuthViewModel) {
    val navController = rememberNavController()
    val currentUser by authVM.currentUser.collectAsState()
    val navBackStackEntry by navController.currentBackStackEntryAsState()
    val currentRoute = navBackStackEntry?.destination?.route

    val tabs = buildList {
        add(BmuRoute.Dashboard)
        add(BmuRoute.Soh)
        add(BmuRoute.Fleet)
        add(BmuRoute.System)
        add(BmuRoute.Audit)
        if (currentUser?.role?.canConfigure == true) {
            add(BmuRoute.Config)
        }
    }

    Scaffold(
        topBar = { StatusBar() },
        bottomBar = {
            NavigationBar {
                tabs.forEach { tab ->
                    NavigationBarItem(
                        icon = { Icon(tab.icon, contentDescription = tab.label) },
                        label = { Text(tab.label) },
                        selected = currentRoute == tab.route,
                        onClick = {
                            navController.navigate(tab.route) {
                                popUpTo(navController.graph.findStartDestination().id) {
                                    saveState = true
                                }
                                launchSingleTop = true
                                restoreState = true
                            }
                        },
                    )
                }
            }
        },
    ) { innerPadding ->
        NavHost(
            navController = navController,
            startDestination = BmuRoute.Dashboard.route,
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding),
        ) {
            composable(BmuRoute.Dashboard.route) {
                DashboardScreen(
                    onBatteryClick = { index ->
                        navController.navigate("battery_detail/$index")
                    },
                )
            }

            composable(BmuRoute.Soh.route) {
                SohDashboardScreen(
                    onBatteryClick = { index ->
                        navController.navigate("battery_detail/$index")
                    },
                )
            }

            composable(BmuRoute.Fleet.route) {
                FleetScreen(
                    onBatteryClick = { index ->
                        navController.navigate("battery_detail/$index")
                    },
                )
            }

            composable(
                route = "battery_detail/{batteryIndex}",
                arguments = listOf(navArgument("batteryIndex") { type = NavType.IntType }),
            ) { backStackEntry ->
                val batteryIndex = backStackEntry.arguments?.getInt("batteryIndex") ?: 0
                BatteryDetailScreen(
                    batteryIndex = batteryIndex,
                    authVM = authVM,
                )
            }

            composable(BmuRoute.System.route) {
                SystemScreen()
            }

            composable(BmuRoute.Audit.route) {
                AuditScreen()
            }

            composable(BmuRoute.Config.route) {
                ConfigScreen(
                    onNavigateProtection = { navController.navigate("config/protection") },
                    onNavigateWifi = { navController.navigate("config/wifi") },
                    onNavigateUsers = { navController.navigate("config/users") },
                    onNavigateSync = { navController.navigate("config/sync") },
                    onNavigateTransport = { navController.navigate("config/transport") },
                )
            }

            composable("config/protection") { ProtectionConfigScreen() }
            composable("config/wifi") { WifiConfigScreen() }
            composable("config/users") { UserManagementScreen() }
            composable("config/sync") { SyncConfigScreen() }
            composable("config/transport") { TransportConfigScreen() }
        }
    }
}
