# HEADDRUMZ KNOCK 0.4.8

KNOCK is a stereo drum dynamics insert built with inMusic’s official MPC Plugin Builder SDK. This build targets MPC XL standalone through the SDK’s 64-bit ARM binary. It has passed desktop and package validation. Loading, touch layout, meter refresh, project recall, and CPU use on a physical MPC XL still need verification.

## Install on MPC XL

1. Check that the MPC XL is running **MPC OS 3.10.1 or later**. On the XL, hold **Shift** and press **Menu / Prefs** to open Preferences. In **Enhancements (Beta)**, turn on **Enable Enhancements** and **External Plugin Support**.
2. Unzip the MPC package on your computer. On a USB drive or SD card, create a folder named exactly `VST2 Plugins` at the root. Copy the intact **HEADDRUMZ - VST - KNOCK** folder inside it. The path should be `VST2 Plugins/HEADDRUMZ - VST - KNOCK/plugin.json`. Leave the binaries and **Plugin Skins** inside their folders.
3. Insert the drive into the MPC XL in Standalone Mode. In a test project, open the mixer or track’s insert effects. On an empty insert on a stereo Drum track or drum submix, browse **External Plugins**, select **KNOCK**, and open the insert editor.
4. Start with **West Coast** and play a stereo drum loop. Pull **Mix** to 0% with **Output** at 0 dB to hear the dry input. Bring **Mix** back up, level-match with **Output**, and compare against bypass.
5. Play a loop and watch **Input Peak** move. Stop playback and let the bar fall while the insert still receives silence. Turn **Threshold** down while playing and watch **Gain Reduction** respond. Try the six presets and both mode selectors. Check touch editing, Q-Links in Screen mode, and that the HEADDRUMZ artwork has **KNOCK** underneath. Save the test project, reopen it with the drive attached, and verify the plugin, preset and edited controls.
6. Watch the MPC CPU meter with one instance and listen for dropouts as you change presets and automate Mix. To remove the test build, remove its folder from `VST2 Plugins` and restart the MPC if it still appears.

More information from [Akai’s External Plugin Support FAQ](https://support.akaipro.com/en/support/solutions/articles/69000885284-mpc-3-10-1-enhancements-frequently-asked-questions) and the [SDK installation guide](https://github.com/inmusic-software/mpc-plugin-builder-sdk/blob/main/docs/install-and-test.md).

## On-screen help

Tap **HELP** beside **KNOCK** to view the control guide and a compression exercise. Tap **KNOCK** to return.

## Controls

| Control | What it does |
| --- | --- |
| Threshold, Attack, Release | Set compression onset and timing. Attack and Release are base values scaled by Style. |
| Transient | Boost or soften the beginning of hits. Zero sets transient gain to unity. |
| Input | Adjust the level entering the wet path and the compressor detector. |
| Drive | Blend in level-compensated saturation. Zero bypasses the saturation. |
| Clip Ceiling, Clip | Set and enable the wet path’s soft clipping. The dry signal and final Output gain can take the mixed output above this ceiling. |
| Output, Mix | Set final gain and dry/wet balance. Zero Mix with 0 dB Output gives unity dry after the controls settle. |
| Source | **Kick** emphasizes onset shaping. **808** protects the low fundamental from driving the detector as hard and reduces transient depth and drive. **Drum Bus** sits between those voicings. |
| Style | **Glue** uses a broad knee and gentle 2:1 slope. **Punch** uses a firmer knee and 4:1 slope. **Crush** uses the fastest timing and an 8:1 slope. |

Factory presets are **West Coast**, **Boom Bap**, **Trap 808**, **Drum & Bass**, **Lo-Fi Hip Hop**, and **Drill Grit**. The effect uses original dynamics and clipping logic with standard MIT-licensed SDK helpers. Compression and transient gain are stereo-linked. There is no lookahead or reported processing latency, and saturation and clipping run at the host sample rate without oversampling. The gain-reduction bar shows compressor attenuation up to 24 dB, before the other wet stages.

The faceplate uses Akai-supported PNG skins, film-strip controls, switches and Q-Link bindings. The HEADDRUMZ art is an edited version of the supplied banner with MEDIA removed. KNOCK appears directly below the art. The panel and mode buttons have baked-in captions for reliable display. Two LED strips show post-input-gain stereo peak and compressor gain reduction. The input strip follows audio even at 0% Mix or when the compressor applies no gain reduction. The plugin requests host display refresh on significant meter changes, up to 25 times per second. Confirm the MPC XL honors these requests in Standalone Mode.

## Build and verify

Use Python 3 and Zig. The tested compiler was Zig 0.15.2. From [SDK revision a5949cf](https://github.com/inmusic-software/mpc-plugin-builder-sdk/tree/a5949cf6fe2cbfb95bcbe0ddf1f7ef555e71f2be), place the source archive’s `KNOCK` folder under `plugins/`, then run:

```sh
python3 tools/build.py plugins/KNOCK
python3 tools/validate.py plugins/KNOCK
```

The source includes the final `skins/` directory, which the build copies verbatim. The simplified `ui.json` records the control layout. The final skin additionally includes the gain-reduction meter and branded footer.

On macOS, run the stock simulator from the SDK root:

```sh
.sdk-tools/host_sim plugins/KNOCK/build/native/knock.dylib test --seconds 3 --wav knock-test.wav
zig c++ -std=c++17 -O2 -Iframework plugins/KNOCK/tests/test_knock.cpp -o plugins/KNOCK/build/test_knock
plugins/KNOCK/build/test_knock plugins/KNOCK/build/native/knock.dylib
```

The stock simulator reports a state mismatch for the live meter readings because it runs audio before comparing the saved and restored parameter values. The source archive includes a simulator-only patch that takes the state snapshot before that render. To reproduce the corrected measurement from a fresh SDK checkout, run:

```sh
patch -p1 < plugins/KNOCK/tests/host-sim-state-first.patch
python3 tools/build.py plugins/KNOCK --target native
.sdk-tools/host_sim plugins/KNOCK/build/native/knock.dylib test --seconds 3
```

The separate drum-signal test checks all six presets and all nine combinations of Source and Style, dry unity, stereo linking, gain-reduction telemetry, zero reported latency and the wet clip ceiling. The SDK package validator checks the ELF binaries, dependencies, identity and skin bindings. Both ARM builds were compiled and inspected. The audio tests ran against the native macOS build using synthetic signals. The 32-bit binary is included in the SDK package layout and remains untested on hardware.

### Audition compression and presets

Use a loop or several repeated kicks and set Mix to 100%. Choose Punch, set
Drive to zero and turn Clip off so the wet path makes the compressor easier
to hear. Move Threshold from 0 dB to about −30 dB and watch Gain Reduction.
With the threshold low, sweep Attack from 0.5 to 60 ms and listen to the
front edge of each hit. Fast attack takes more off the initial hit and slow
attack lets more of it through.

Factory settings are differentiated by envelope, tone, and wet balance.
Compare Boom Bap with Lo-Fi Hip Hop for the clearest attack contrast. Use
Output to level-match presets. Saved parameters keep their existing IDs and
ranges. Selecting a factory preset loads the v0.2.0 settings for that preset.

### Genre presets

- **West Coast** adds low punch and top-end definition with slower compression.
- **Boom Bap** emphasizes the front of hits and adds bite.
- **Trap 808** brings up the low body and softens the top.
- **Drum & Bass** applies fast, hard compression, parallel distortion and clipping.
- **Lo-Fi Hip Hop** rounds hits and rolls back the top.
- **Drill Grit** adds midrange distortion and harmonics to 808s.

Use **TONE** to adjust Weight (low shelf), Snap (high shelf) and Dirt
(parallel distortion with a high-pass return). Their defaults are neutral.
For the audio comparison, each section uses the same synthetic beat at matched
RMS. The clip opens with the dry beat and follows the preset order above.

### Digital EQ

Tap **EQ** for a 90 Hz low shelf, a sweepable mid bell (120 Hz to 8 kHz),
a width control (Q 0.4 to 5), and a 7.5 kHz high shelf. Each band cuts or
boosts up to 12 dB. Use the EQ switch to compare with the EQ bypassed.
The shelves and bell start flat in all six genre presets. EQ affects the wet
signal after Tone and before the soft clipper. At 0% Mix the signal remains dry.
The response test measures low, mid, and high level changes on both channels
and verifies the flat, bypassed and fully dry paths.

### A/B and EQ response

Tap **A/B**. Normal uses your Output setting, A Dry listens to the input at unity,
and B Match hears the processed path with up to 12 dB of automatic RMS matching.
Play the same loop for a second before comparing. Matching measures both paths
continuously, works with the selected Mix, and holds its last correction during silence.
Select Normal when you finish comparing. Selecting a factory preset also returns to Normal.

The **EQ** page plots the calculated wet-path EQ response while you edit. It
covers 25 Hz to 20 kHz with a ±24 dB display range. It is a response display,
not a spectrum analyzer, and goes flat when EQ is switched off. The desktop
tests verify the curve values and A/B level compensation. Screen rendering on
the MPC XL is still to be checked.

The TONE tab includes a listening setup: set Mix to 100%, switch Clip off temporarily, select Normal on A/B, then audition one Tone dial at a time. Restore Mix and Clip afterward.

This GUI test changes the main panel order, screen Q-Link order, and logo. Install one version at a time. To return to the reference skin, reinstall 0.3.2 and restart the MPC.

The 0.4.2 GUI adds ivory knobs with tick marks, wider faders, rounded selector buttons and new toggles. It retains the same parameter bindings and sound engine.

### Drive Level needle

Drive Level reads the stereo RMS signal into the Drive waveshaper, after compression and transient shaping and before the saturation blend. The 0 mark is -18 dBFS RMS. Its 125 ms response averages hits. Input Peak and Gain Reduction remain visible. The needle is independent of the final Mix, Output and A/B level matching.

The red HOT light tracks the needle at or above the 0 mark. It indicates drive relative to the meter reference and does not indicate output clipping.

Version 0.4.5 restores the charcoal-and-amber palette and original wide HEADDRUMZ artwork. It retains the 0.4.4 layout and working audio features.

The main panel displays the silver KNOCK title below the wide HEADDRUMZ logo.

## License

KNOCK v0.4.8 is released under the MIT License. See LICENSE. The inMusic SDK license is preserved in SDK-LICENSE.txt.
