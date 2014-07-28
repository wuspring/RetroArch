package com.retroarch.browser.preferences.util;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStreamReader;

import android.annotation.TargetApi;
import android.content.Context;
import android.content.SharedPreferences;
//import android.content.pm.PackageManager;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.os.Build;
import android.preference.PreferenceManager;
import android.util.Log;
import android.view.Display;
import android.view.WindowManager;

/**
 * Utility class for retrieving, saving, or loading preferences.
 */
public final class UserPreferences
{
	// Logging tag.
	private static final String TAG = "UserPreferences";

	/**
	 * Retrieves the path to the default location of the libretro config.
	 * 
	 * @param ctx the current {@link Context}
	 * 
	 * @return the path to the default location of the libretro config.
	 */
	public static String getDefaultConfigPath(Context ctx)
	{
		// Internal/External storage dirs.
		final String internal = System.getenv("INTERNAL_STORAGE");
		final String external = System.getenv("EXTERNAL_STORAGE");

		// Native library directory and data directory for this front-end.
		final String dataDir = ctx.getApplicationInfo().dataDir;
		final String coreDir = dataDir + "/cores/";
		
		// Get libretro name and path
		final SharedPreferences prefs = getPreferences(ctx);
		final String libretro_path = prefs.getString("libretro_path", coreDir);

		// Check if global config is being used. Return true upon failure.
		final boolean globalConfigEnabled = prefs.getBoolean("global_config_enable", true);

		String append_path;
		// If we aren't using the global config.
		if (!globalConfigEnabled && !libretro_path.equals(coreDir))
		{
			String sanitized_name = sanitizeLibretroPath(libretro_path);
			append_path = File.separator + sanitized_name + ".cfg";
		}
		else // Using global config.
		{
			append_path = File.separator + "retroarch.cfg";
		}

		if (external != null)
		{
			String confPath = external + append_path;
			if (new File(confPath).exists())
				return confPath;
		}
		else if (internal != null)
		{
			String confPath = internal + append_path;
			if (new File(confPath).exists())
				return confPath;
		}
		else
		{
			String confPath = "/mnt/extsd" + append_path;
			if (new File(confPath).exists())
				return confPath;
		}

		if (internal != null && new File(internal + append_path).canWrite())
			return internal + append_path;
		else if (external != null && new File(internal + append_path).canWrite())
			return external + append_path;
		else if (dataDir != null)
			return dataDir + append_path;
		else
			// emergency fallback, all else failed
			return "/mnt/sd" + append_path;
	}

	/**
	 * Re-reads the configuration file into the {@link SharedPreferences}
	 * instance that contains all of the settings for the front-end.
	 * 
	 * @param ctx the current {@link Context}.
	 */
	public static void readbackConfigFile(Context ctx)
	{
		String path = getDefaultConfigPath(ctx);
		ConfigFile config = new ConfigFile(path);

		Log.i(TAG, "Config readback from: " + path);
		
		SharedPreferences prefs = getPreferences(ctx);
		SharedPreferences.Editor edit = prefs.edit();

		// General Settings
		readbackBool(config, edit, "rewind_enable");
		readbackString(config, edit, "rewind_granularity");
		readbackBool(config, edit, "savestate_auto_load");
		readbackBool(config, edit, "savestate_auto_save");

		// Audio Settings.
		// TODO: Other audio settings
		readbackBool(config, edit, "audio_rate_control");
		readbackBool(config, edit, "audio_enable");

		// Input Settings
		readbackString(config, edit, "input_overlay");
		readbackBool(config, edit, "input_overlay_enable");
		readbackDouble(config, edit, "input_overlay_opacity");
		readbackBool(config, edit, "input_autodetect_enable");

		// Video Settings
		readbackBool(config, edit, "video_scale_integer");
		readbackBool(config, edit, "video_smooth");
		readbackBool(config, edit, "video_threaded");
		readbackBool(config, edit, "video_allow_rotate");
		readbackBool(config, edit, "video_font_enable");
		readbackBool(config, edit, "video_vsync");
		readbackString(config, edit, "video_refresh_rate");

		// Path settings
		readbackString(config, edit, "rgui_browser_directory");
		readbackString(config, edit, "savefile_directory");
		readbackString(config, edit, "savestate_directory");
		readbackBool(config, edit, "savefile_directory_enable"); // Ignored by RetroArch
		readbackBool(config, edit, "savestate_directory_enable"); // Ignored by RetroArch

		edit.commit();
	}

	/**
	 * Updates the libretro configuration file
	 * with new values if settings have changed.
	 * 
	 * @param ctx the current {@link Context}.
	 */
	public static void updateConfigFile(Context ctx)
	{
		String path = getDefaultConfigPath(ctx);
		ConfigFile config = new ConfigFile(path);

		Log.i(TAG, "Writing config to: " + path);

		final String dataDir = ctx.getApplicationInfo().dataDir;
		final String coreDir = dataDir + "/cores/";

		final SharedPreferences prefs = getPreferences(ctx);
		
		config.setString("libretro_path", prefs.getString("libretro_path", coreDir));
		config.setString("libretro_directory", coreDir);
		config.setString("rgui_browser_directory", prefs.getString("rgui_browser_directory", ""));
		config.setBoolean("audio_rate_control", prefs.getBoolean("audio_rate_control", true));
		config.setInt("audio_out_rate", getOptimalSamplingRate(ctx));

		// Refactor this entire mess and make this usable for per-core config
		if (Build.VERSION.SDK_INT >= 17 && prefs.getBoolean("audio_latency_auto", true))
		{
			config.setInt("audio_block_frames", getLowLatencyBufferSize(ctx));
		}

		config.setBoolean("audio_enable", prefs.getBoolean("audio_enable", true));
		config.setBoolean("video_smooth", prefs.getBoolean("video_smooth", true));
		config.setBoolean("video_allow_rotate", prefs.getBoolean("video_allow_rotate", true));
		config.setBoolean("savestate_auto_load", prefs.getBoolean("savestate_auto_load", true));
		config.setBoolean("savestate_auto_save", prefs.getBoolean("savestate_auto_save", false));
		config.setBoolean("rewind_enable", prefs.getBoolean("rewind_enable", false));
		config.setInt("rewind_granularity", Integer.parseInt(prefs.getString("rewind_granularity", "1")));
		config.setBoolean("video_vsync", prefs.getBoolean("video_vsync", true));
		config.setBoolean("input_autodetect_enable", prefs.getBoolean("input_autodetect_enable", true));
		config.setString("video_refresh_rate", prefs.getString("video_refresh_rate", ""));
		config.setBoolean("video_threaded", prefs.getBoolean("video_threaded", true));

		// Refactor these weird values - 'full', 'auto', 'square', whatever -
		// go by what we have in the menu - makes maintaining state easier too
		String aspect = prefs.getString("video_aspect_ratio", "auto");
		if (aspect.equals("full"))
		{
			config.setBoolean("video_force_aspect", false);
		}
		else if (aspect.equals("auto"))
		{
			config.setBoolean("video_force_aspect", true);
			config.setBoolean("video_force_aspect_auto", true);
			config.setDouble("video_aspect_ratio", -1.0);
		}
		else if (aspect.equals("square"))
		{
			config.setBoolean("video_force_aspect", true);
			config.setBoolean("video_force_aspect_auto", false);
			config.setDouble("video_aspect_ratio", -1.0);
		}
		else
		{
			double aspect_ratio = Double.parseDouble(aspect);
			config.setBoolean("video_force_aspect", true);
			config.setDouble("video_aspect_ratio", aspect_ratio);
		}

		config.setBoolean("video_scale_integer", prefs.getBoolean("video_scale_integer", false));
		config.setString("video_shader", prefs.getString("video_shader", ""));
		config.setBoolean("video_shader_enable", prefs.getBoolean("video_shader_enable", false) &&
				new File(prefs.getString("video_shader", "")).exists());

		if (prefs.contains("input_overlay_enable"))
			config.setBoolean("input_overlay_enable", prefs.getBoolean("input_overlay_enable", true));
		config.setString("input_overlay", prefs.getString("input_overlay", ""));

		if (prefs.getBoolean("savefile_directory_enable", false))
		{
		   config.setString("savefile_directory", prefs.getString("savefile_directory", ""));
		}
		if (prefs.getBoolean("savestate_directory_enable", false))
		{
		   config.setString("savestate_directory", prefs.getString("savestate_directory", ""));
		}
		if (prefs.getBoolean("system_directory_enable", false))
		{
		   config.setString("system_directory", prefs.getString("system_directory", ""));
		}

		config.setBoolean("video_font_enable", prefs.getBoolean("video_font_enable", true));
		config.setString("game_history_path", dataDir + "/retroarch-history.txt");

		// FIXME: This is incomplete. Need analog axes as well.
		for (int i = 1; i <= 4; i++)
		{
			final String[] btns =
			{ 
				"up", "down", "left", "right",
				"a", "b", "x", "y", "start", "select",
				"l", "r", "l2", "r2", "l3", "r3"
			};

			for (String b : btns)
			{
				String p = "input_player" + i + "_" + b + "_btn";
				if (prefs.contains(p))
					config.setInt(p, prefs.getInt(p, 0));
				else
					config.setString(p, "nul");
			}
		}

		try
		{
			config.write(path);
		}
		catch (IOException e)
		{
			Log.e(TAG, "Failed to save config file to: " + path);
		}
	}

	private static void readbackString(ConfigFile cfg, SharedPreferences.Editor edit, String key)
	{
		if (cfg.keyExists(key))
			edit.putString(key, cfg.getString(key));
		else
			edit.remove(key);
	}

	private static void readbackBool(ConfigFile cfg, SharedPreferences.Editor edit, String key)
	{
		if (cfg.keyExists(key))
			edit.putBoolean(key, cfg.getBoolean(key));
		else
			edit.remove(key);
	}

	private static void readbackDouble(ConfigFile cfg, SharedPreferences.Editor edit, String key)
	{
		if (cfg.keyExists(key))
			edit.putFloat(key, (float)cfg.getDouble(key));
		else
			edit.remove(key);
	}

	/*
	private static void readbackFloat(ConfigFile cfg, SharedPreferences.Editor edit, String key)
	{
		if (cfg.keyExists(key))
			edit.putFloat(key, cfg.getFloat(key));
		else
			edit.remove(key);
	}
	*/

	/**
	private static void readbackInt(ConfigFile cfg, SharedPreferences.Editor edit, String key)
	{
		if (cfg.keyExists(key))
			edit.putInt(key, cfg.getInt(key));
		else
			edit.remove(key);
	}
	*/

	/**
	 * Sanitizes a libretro core path.
	 * 
	 * @param path The path to the libretro core.
	 * 
	 * @return the sanitized libretro path.
	 */
	private static String sanitizeLibretroPath(String path)
	{
		String sanitized_name = path.substring(
				path.lastIndexOf('/') + 1,
				path.lastIndexOf('.'));
		sanitized_name = sanitized_name.replace("neon", "");
		sanitized_name = sanitized_name.replace("libretro_", "");

		return sanitized_name;
	}

	/**
	 * Gets a {@link SharedPreferences} instance containing current settings.
	 * 
	 * @param ctx the current {@link Context}.
	 * 
	 * @return A SharedPreference instance containing current settings.
	 */
	public static SharedPreferences getPreferences(Context ctx)
	{
		return PreferenceManager.getDefaultSharedPreferences(ctx);
	}

	/**
	 * Gets the optimal sampling rate for low-latency audio playback.
	 * 
	 * @param ctx the current {@link Context}.
	 * 
	 * @return the optimal sampling rate for low-latency audio playback in Hz.
	 */
	@TargetApi(17)
	private static int getLowLatencyOptimalSamplingRate(Context ctx)
	{
		AudioManager manager = (AudioManager) ctx.getSystemService(Context.AUDIO_SERVICE);

		return Integer.parseInt(manager
				.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE));
	}

	/**
	 * Gets the optimal buffer size for low-latency audio playback.
	 * 
	 * @param ctx the current {@link Context}.
	 * 
	 * @return the optimal output buffer size in decimal PCM frames.
	 */
	@TargetApi(17)
	private static int getLowLatencyBufferSize(Context ctx)
	{
		AudioManager manager = (AudioManager) ctx.getSystemService(Context.AUDIO_SERVICE);
		int buffersize = Integer.parseInt(manager
				.getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER));
		Log.i(TAG, "Queried ideal buffer size (frames): " + buffersize);
		return buffersize;
	}

	/**
	 * Gets the optimal audio sampling rate.
	 * <p>
	 * On Android 4.2+ devices this will retrieve the optimal low-latency sampling rate,
	 * since Android 4.2 adds support for low latency audio in general.
	 * <p>
	 * On other devices, it simply returns the regular optimal sampling rate
	 * as returned by the hardware.
	 * 
	 * @param ctx The current {@link Context}.
	 * 
	 * @return the optimal audio sampling rate in Hz.
	 */
	private static int getOptimalSamplingRate(Context ctx)
	{
		int ret;
		if (Build.VERSION.SDK_INT >= 17)
			ret = getLowLatencyOptimalSamplingRate(ctx);
		else
			ret = AudioTrack.getNativeOutputSampleRate(AudioManager.STREAM_MUSIC);

		Log.i(TAG, "Using sampling rate: " + ret + " Hz");
		return ret;
	}

	/**
	 * Retrieves the CPU info, as provided by /proc/cpuinfo.
	 * 
	 * @return the CPU info.
	 */
	public static String readCPUInfo()
	{
		StringBuilder result = new StringBuilder(255);

		try
		{
			BufferedReader br = new BufferedReader(new InputStreamReader(
					new FileInputStream("/proc/cpuinfo")));

			String line;
			while ((line = br.readLine()) != null)
				result.append(line).append('\n');
			br.close();
		}
		catch (IOException ex)
		{
			ex.printStackTrace();
		}

		return result.toString();
	}
}
