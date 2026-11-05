package com.gtavrecomp.app;

import android.os.Bundle;
import android.util.Log;
import android.view.WindowManager;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.PrintStream;
import java.util.ArrayList;

import org.libsdl.app.SDLActivity;

/**
 * SDL activity that runs the recompiled game (libmain.so, SDL_main). Game data lives in the app's
 * external files directory (adb-writable without storage permissions):
 *   files/game/disc2_files, files/game/update, files/userdata; the log goes to files/logs/gtav.log.
 * files/args.txt adds runtime flags, one per line (lines starting with # are ignored); a line
 * env:NAME=VALUE sets an environment variable for the game instead.
 */
public class GameActivity extends SDLActivity {
    private static final String TAG = "GtavRecomp";

    private static void writeCrashLog(String msg, Throwable t) {
        String[] paths = {
            "/storage/emulated/0/gtav/crash.log",
            "/sdcard/gtav_crash.log"
        };
        for (String p : paths) {
            try {
                File f = new File(p);
                f.getParentFile().mkdirs();
                try (PrintStream ps = new PrintStream(new FileOutputStream(f, true))) {
                    ps.println("=== CRASH LOG ===");
                    if (msg != null) ps.println(msg);
                    if (t != null) t.printStackTrace(ps);
                    ps.println();
                }
            } catch (Exception ignored) {
            }
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        Thread.setDefaultUncaughtExceptionHandler((thread, throwable) -> {
            Log.e(TAG, "Uncaught exception in " + thread.getName(), throwable);
            writeCrashLog("Uncaught exception in thread " + thread.getName(), throwable);
        });

        // SDL picks the orientation from the window it creates (the runtime's is resizable, so
        // portrait was allowed): the game is landscape only.
        try {
            android.system.Os.setenv("SDL_ORIENTATIONS", "LandscapeLeft LandscapeRight", true);
            // No accelerometer-as-joystick and no HIDAPI: both keep SDL's event loop polling (a
            // whole big core spent in joystick detection); pads come through Android's input events.
            android.system.Os.setenv("SDL_ACCELEROMETER_AS_JOYSTICK", "0", true);
            android.system.Os.setenv("SDL_JOYSTICK_HIDAPI", "0", true);
        } catch (Exception e) {
            Log.w(TAG, "setenv: " + e);
        }
        // args.txt env: lines before super.onCreate loads libmain.so: its static initializers read
        // some variables (GTAV_NATIVE_FETCH_RESOLVE), and getArguments runs only after the load.
        File extra = new File(getExternalFilesDir(null), "args.txt");
        if (extra.exists()) {
            try (BufferedReader reader = new BufferedReader(new FileReader(extra))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    line = line.trim();
                    if (line.startsWith("env:") && line.indexOf('=') > 4) {
                        int eq = line.indexOf('=');
                        android.system.Os.setenv(line.substring(4, eq), line.substring(eq + 1), true);
                    }
                }
            } catch (Exception e) {
                Log.w(TAG, "args.txt env: " + e);
            }
        }
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    @Override
    protected String[] getLibraries() {
        // SDL3 is linked statically into librexruntime.so; loading libmain.so pulls in its deps.
        return new String[] { "main" };
    }

    /**
     * The public APK: no game code (libmain.so is a shim; libgtav_fast.so holds ours and the
     * player's libgtav_game.so comes from their PC's Build for Android, see SetupActivity).
     */
    static boolean isReleaseLayout(android.content.Context c) {
        return new File(c.getApplicationInfo().nativeLibraryDir, "libgtav_fast.so").exists();
    }

    /** The release defaults (tools/android/args.txt): files/args.txt lines come after and win. */
    private static final String[] RELEASE_ARGS = {
        "--headless=true", "--log_level=info", "--gtav_native=on", "--gtav_fps_limit=60",
        "--gpu_skip_rendering=true", "--gpu_skip_rendering_lag_ms=4", "--vsync=false",
        "--gtav_native_vsync=true", "--gtav_native_fps=true", "--vulkan_log_debug_messages=false",
        "--window_presenter=false", "--gtav_native_texture_budget_mb=768",
        "--gtav_native_fetch_resolves=false", "--gtav_native_rect_gs=false",
        "--gtav_native_render_scale=1.0", "--gtav_android_main_prime=true",
        "--audio_maxqframes=32", "--audio_device_frames=512", "--audio_force_stereo=true",
        "--vulkan_require_vertex_pipeline_stores_and_atomics=false",
        "--vulkan_require_geometry_shader=false",
        "--vulkan_require_fill_mode_non_solid=false",
    };

    @Override
    protected String[] getArguments() {
        File files = getExternalFilesDir(null);
        String root = files.getAbsolutePath();
        new File(files, "logs").mkdirs();
        ArrayList<String> args = new ArrayList<>();
        if (new File(files, "data/data_installed.txt").exists()) {
            // Installed by the setup screen (install.cpp RunDataInstall layout).
            new File(files, "data/user").mkdirs();
            args.add("--game_data_root=" + root + "/data/game");
            args.add("--update_data_root=" + root + "/data/update");
            args.add("--user_data_root=" + root + "/data/user");
        } else {
            // Developer layout (pushed with adb).
            new File(files, "userdata").mkdirs();
            args.add("--game_data_root=" + root + "/game/disc2_files");
            args.add("--update_data_root=" + root + "/game/update");
            args.add("--user_data_root=" + root + "/userdata");
        }
        if (isReleaseLayout(this)) {
            for (String a : RELEASE_ARGS) args.add(a);
        }
        args.add("--gpu_plugin=xenos");

        // Prefer writing log directly to /storage/emulated/0/gtav/logs/gtav.log if available
        File gtavDir = new File("/storage/emulated/0/gtav");
        String logFile = root + "/logs/gtav.log";
        if (gtavDir.exists()) {
            File logDir = new File(gtavDir, "logs");
            logDir.mkdirs();
            if (logDir.canWrite()) {
                logFile = new File(logDir, "gtav.log").getAbsolutePath();
            }
        }
        args.add("--log_file=" + logFile);
        args.add("--log_max_file_size_mb=256");
        args.add("--log_max_files=2");

        File extra = new File(files, "args.txt");
        File externalArgs = new File(gtavDir, "args.txt");
        if (externalArgs.exists()) {
            extra = externalArgs;
        }
        if (extra.exists()) {
            try (BufferedReader reader = new BufferedReader(new FileReader(extra))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    line = line.trim();
                    if (!line.isEmpty() && !line.startsWith("#") && !line.startsWith("env:")) {
                        args.add(line);
                    }
                }
            } catch (Exception e) {
                Log.w(TAG, "args.txt: " + e);
            }
        }
        // One value per flag, the last one winning (files/args.txt over the defaults): the
        // command-line parser (CLI11) rejects a repeated option and then applies none of them.
        java.util.LinkedHashMap<String, String> byName = new java.util.LinkedHashMap<>();
        for (String a : args) {
            int eq = a.indexOf('=');
            String name = eq < 0 ? a : a.substring(0, eq);
            if (name.startsWith("--no-")) name = "--" + name.substring(5);
            byName.remove(name);
            byName.put(name, a);
        }
        args = new ArrayList<>(byName.values());
        String fullCmd = String.join(" ", args);
        Log.i(TAG, "arguments: " + fullCmd);

        // Record launch arguments
        try {
            File launchLog = new File("/storage/emulated/0/gtav/launch.log");
            launchLog.getParentFile().mkdirs();
            try (PrintStream ps = new PrintStream(new FileOutputStream(launchLog, false))) {
                ps.println("=== Launch Arguments ===");
                ps.println(fullCmd);
            }
        } catch (Exception ignored) {
        }

        return args.toArray(new String[0]);
    }
}
