package org.ghostnet.monitor

import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import org.junit.After
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/**
 * Runs the real MainActivity, Application and all four Compose tabs on the
 * JVM (Robolectric). Native code is not loaded here; the native receive
 * chain has its own host and arm64 tests. This catches composition-time
 * crashes and checks that each tab renders its key controls, including
 * while a (simulated) session is receiving.
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
class UiSmokeTest {
    @get:Rule
    val compose = createAndroidComposeRule<MainActivity>()

    @After
    fun reset() {
        MonitorState.setPhase(MonitorState.Phase.IDLE)
    }

    @Test
    fun allFourTabsRender() {
        compose.onNodeWithText("START RECEIVING").assertExists()
        compose.onNodeWithText("Device (PROBLEM): No RTL-SDR connected", substring = true).assertExists()

        compose.onNodeWithText("Schedule").performClick()
        compose.onNodeWithText("GhostNet schedule").assertExists()
        assertTrue(compose.onAllNodesWithText("Booklet page", substring = true).fetchSemanticsNodes().isNotEmpty())

        compose.onNodeWithText("Export").performClick()
        compose.onNodeWithText("Send report").assertExists()
        compose.onNodeWithText("Diagnostics only").assertExists()

        compose.onNodeWithText("Settings").performClick()
        compose.onNodeWithText("Run self-test").assertExists()
        compose.onNodeWithText("Invert spectrum (HF)").assertExists()
        compose.onNodeWithText("Reverse (swap mark and space)").assertExists()
    }

    @Test
    fun receiveModesSwitchPanelsAndComingSoonIsInert() {
        compose.onNodeWithText("ALE · Coming Soon").assertExists()
        compose.onNodeWithText("ALE · Coming Soon").performClick()
        compose.onNodeWithText("START RECEIVING").assertExists() // nothing started

        compose.onNodeWithText("RTTY").performClick()
        compose.onNodeWithText("Receiving now").assertExists()
        compose.onNodeWithText("40 m · 7.077 MHz RTTY").assertExists()

        compose.onNodeWithText("Voice (FM)").performClick()
        compose.onNodeWithText("Audio is not recorded or logged").assertExists()
        compose.onNodeWithText("Squelch (mute when no signal)").assertExists()
        compose.onNodeWithText("START LISTENING").assertExists()

        compose.onNodeWithText("Voice (SSB)").performClick()
        compose.onNodeWithText("40 m · 7.190 MHz LSB voice").assertExists()

        compose.onNodeWithText("JS8").performClick()
        compose.onNodeWithText("Messages").assertExists()
    }

    @Test
    fun receiveTabShowsLiveSession() {
        val settings = ReceiveSettings(Presets.byId("40m"), invertSpectrum = false, rtlAgc = true, ppm = 0)
        val now = System.currentTimeMillis()
        MonitorState.beginSession(settings, "test-session", now, "RTL-SDR Blog V3 (test)", null)
        MonitorState.setPhase(MonitorState.Phase.RECEIVING)
        MonitorState.updateStats(
            NativeReceiver.Stats(
                running = true, iqBytes = 1_000_000, droppedBytes = 0, iqLevelDbfs = -30.0, clipPercent = 0.0,
                channelLevelDbfs = -60.0, agcGainDb = 40.0, decodePasses = 12, frames = 4, messages = 1,
                medianDtS = 0.1, dtSamples = 4, lastIqUnixMs = now, lastFrameUnixMs = now,
                spectrumPowerDb = 0.0, spectrumPeakDb = 0.0, recording = false, recordedBytes = 0,
            ),
        )
        MonitorState.addMessage(
            MessageRecord(now, now + 45_000, 0, 1500.0, -12, -10, 4, true, false, false, false,
                "KN4CRD: @GHOSTNET TEST MESSAGE", "KN4CRD", "@GHOSTNET", "", "a b c d"),
        )
        repeat(50) { Waterfall.addRow(FloatArray(300) { i -> if (i in 150..154) -20f else -60f }) }
        compose.activity.tick.intValue++
        compose.waitForIdle()

        compose.onNodeWithText("STOP").assertExists()
        compose.onNodeWithText("KN4CRD: @GHOSTNET TEST MESSAGE").assertExists()
        compose.onNodeWithText("Signal (OK)", substring = true).assertExists()
        compose.onNodeWithText("4 frames · 1 messages", substring = true).assertExists()
    }
}
