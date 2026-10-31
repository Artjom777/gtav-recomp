package com.gtavrecomp.app;

import android.os.Bundle;
import android.util.Log;
import android.view.WindowManager;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
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

    @Override
    protected void onCreate(Bundle savedInstanceState) {
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
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    @Override
    protected String[] getLibraries() {
        // SDL3 is linked statically into librexruntime.so; loading libmain.so pulls in its deps.
        return new String[] { "main" };
    }

    @Override
    protected String[] getArguments() {
        File files = getExternalFilesDir(null);
        String root = files.getAbsolutePath();
        new File(files, "logs").mkdirs();
        new File(files, "userdata").mkdirs();
        ArrayList<String> args = new ArrayList<>();
        args.add("--game_data_root=" + root + "/game/disc2_files");
        args.add("--update_data_root=" + root + "/game/update");
        args.add("--user_data_root=" + root + "/userdata");
        args.add("--gpu_plugin=xenos");
        args.add("--log_file=" + root + "/logs/gtav.log");
        args.add("--log_max_file_size_mb=256");
        args.add("--log_max_files=2");
        File extra = new File(files, "args.txt");
        if (extra.exists()) {
            try (BufferedReader reader = new BufferedReader(new FileReader(extra))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    line = line.trim();
                    if (line.startsWith("env:") && line.indexOf('=') > 4) {
                        int eq = line.indexOf('=');
                        android.system.Os.setenv(line.substring(4, eq), line.substring(eq + 1), true);
                    } else if (!line.isEmpty() && !line.startsWith("#")) {
                        args.add(line);
                    }
                }
            } catch (Exception e) {
                Log.w(TAG, "args.txt: " + e);
            }
        }
        Log.i(TAG, "arguments: " + String.join(" ", args));
        return args.toArray(new String[0]);
    }
}
