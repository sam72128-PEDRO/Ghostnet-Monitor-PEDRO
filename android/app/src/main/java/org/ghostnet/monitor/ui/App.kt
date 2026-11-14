package org.ghostnet.monitor.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.DateRange
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Share
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier

/** Spec section 7.1: exactly four primary tabs. */
@Composable
fun GhostNetMonitorApp(controller: AppController) {
    var tab by rememberSaveable { mutableIntStateOf(0) }
    val tabs = listOf(
        "Receive" to Icons.Filled.PlayArrow,
        "Schedule" to Icons.Filled.DateRange,
        "Export" to Icons.Filled.Share,
        "Settings" to Icons.Filled.Settings,
    )
    Scaffold(
        bottomBar = {
            NavigationBar {
                tabs.forEachIndexed { i, (label, icon) ->
                    NavigationBarItem(
                        selected = tab == i,
                        onClick = { tab = i },
                        icon = { Icon(icon, contentDescription = label) },
                        label = { Text(label) },
                    )
                }
            }
        },
    ) { padding ->
        Box(Modifier.fillMaxSize().padding(padding)) {
            when (tab) {
                0 -> ReceiveScreen(controller)
                1 -> ScheduleScreen(controller, onTune = { tab = 0 })
                2 -> ExportScreen(controller)
                else -> SettingsScreen(controller)
            }
        }
    }
}
