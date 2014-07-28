package com.retroarch.browser.mainmenu;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;

import android.app.AlertDialog;
import android.app.Dialog;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager.NameNotFoundException;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.preference.Preference;
import android.preference.Preference.OnPreferenceClickListener;
import android.preference.PreferenceManager;
import android.provider.Settings;
import android.util.Log;
import android.widget.Toast;

import com.retroarch.R;
import com.retroarch.browser.CoreSelection;
import com.retroarch.browser.HistorySelection;
import com.retroarch.browser.NativeInterface;
import com.retroarch.browser.dirfragment.DetectCoreDirectoryFragment;
import com.retroarch.browser.dirfragment.DirectoryFragment;
import com.retroarch.browser.dirfragment.DirectoryFragment.OnDirectoryFragmentClosedListener;
import com.retroarch.browser.mainmenu.gplwaiver.GPLWaiverDialogFragment;
import com.retroarch.browser.preferences.fragments.util.PreferenceListFragment;
import com.retroarch.browser.preferences.util.UserPreferences;
import com.retroarch.browser.retroactivity.RetroActivityFuture;
import com.retroarch.browser.retroactivity.RetroActivityPast;

/**
 * Represents the fragment that handles the layout of the main menu.
 */
public final class MainMenuFragment extends PreferenceListFragment implements OnPreferenceClickListener, OnDirectoryFragmentClosedListener
{
	private static final String TAG = "MainMenuFragment";
	private Context ctx;
	
	public Intent getRetroActivity()
	{
		if ((Build.VERSION.SDK_INT >= Build.VERSION_CODES.HONEYCOMB))
		{
			return new Intent(ctx, RetroActivityFuture.class);
		}
		return new Intent(ctx, RetroActivityPast.class);
	}

	@Override
	public void onCreate(Bundle savedInstanceState)
	{
		super.onCreate(savedInstanceState);

		// Cache the context
		this.ctx = getActivity();

		// Add the layout through the XML.
		addPreferencesFromResource(R.xml.main_menu);

		// Set the listeners for the menu items
		findPreference("resumeContentPref").setOnPreferenceClickListener(this);
		findPreference("loadCorePref").setOnPreferenceClickListener(this);
		findPreference("loadContentAutoPref").setOnPreferenceClickListener(this);
		findPreference("loadContentPref").setOnPreferenceClickListener(this);
		findPreference("loadContentHistoryPref").setOnPreferenceClickListener(this);
		findPreference("quitRetroArch").setOnPreferenceClickListener(this);

		// Extract assets. 
		extractAssets();

		final SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(ctx);
		if (!prefs.getBoolean("first_time_refreshrate_calculate", false))
		{
			prefs.edit().putBoolean("first_time_refreshrate_calculate", true).commit();

			AlertDialog.Builder alert = new AlertDialog.Builder(ctx)
						.setTitle(R.string.welcome_to_retroarch)
						.setMessage(R.string.welcome_to_retroarch_desc)
						.setPositiveButton(R.string.ok, null);
			alert.show();

			// First-run, so we show the GPL waiver agreement dialog.
			GPLWaiverDialogFragment.newInstance().show(getFragmentManager(), "gplWaiver");
			UserPreferences.updateConfigFile(ctx);
		}
	}

	private void extractAssets()
	{
		if (areAssetsExtracted())
			return;

		final Dialog dialog = new Dialog(ctx);
		final Handler handler = new Handler();
		dialog.setContentView(R.layout.assets);
		dialog.setCancelable(false);
		dialog.setTitle(R.string.asset_extraction);

		// Java is fun :)
		Thread assetsThread = new Thread(new Runnable()
		{
			public void run()
			{
				extractAssetsThread();
				handler.post(new Runnable()
				{
					public void run()
					{
						dialog.dismiss();
					}
				});
			}
		});
		assetsThread.start();

		dialog.show();
	}

	// Extract assets from native code. Doing it from Java side is apparently unbearably slow ...
	private void extractAssetsThread()
	{
		try
		{
			final String dataDir = ctx.getApplicationInfo().dataDir;
			final String apk = ctx.getApplicationInfo().sourceDir;

			Log.i(TAG, "Extracting RetroArch assets from: " + apk + " ...");
			boolean success = NativeInterface.extractArchiveTo(apk, "assets", dataDir);
			if (!success) {
				throw new IOException("Failed to extract assets ...");
			}
			Log.i(TAG, "Extracted assets ...");

			File cacheVersion = new File(dataDir, ".cacheversion");
			DataOutputStream outputCacheVersion = new DataOutputStream(new FileOutputStream(cacheVersion, false));
			outputCacheVersion.writeInt(getVersionCode());
			outputCacheVersion.close();
		}
		catch (IOException e)
		{
			Log.e(TAG, "Failed to extract assets to cache.");
		}
	}

	private boolean areAssetsExtracted()
	{
		int version = getVersionCode();

		try
		{
			String dataDir = ctx.getApplicationInfo().dataDir;
			File cacheVersion = new File(dataDir, ".cacheversion");
			if (cacheVersion.isFile() && cacheVersion.canRead() && cacheVersion.canWrite())
			{
				DataInputStream cacheStream = new DataInputStream(new FileInputStream(cacheVersion));
				int currentCacheVersion = 0;
				try
				{
					currentCacheVersion = cacheStream.readInt();
					cacheStream.close();
				}
				catch (IOException ignored)
				{
				}

				if (currentCacheVersion == version)
				{
					Log.i("ASSETS", "Assets already extracted, skipping...");
					return true;
				}
			}
		}
		catch (IOException e)
		{
			Log.e(TAG, "Failed to extract assets to cache.");
			return false;
		}

		return false;
	}

	private int getVersionCode()
	{
		int version = 0;
		try
		{
			version = ctx.getPackageManager().getPackageInfo(ctx.getPackageName(), 0).versionCode;
		}
		catch (NameNotFoundException ignored)
		{
		}

		return version;
	}
	
	@Override
	public boolean onPreferenceClick(Preference preference)
	{
		final String prefKey = preference.getKey();

		// Resume Content
		if (prefKey.equals("resumeContentPref"))
		{
			UserPreferences.updateConfigFile(ctx);
			final SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(ctx);
			final Intent retro = getRetroActivity();
			
			MainMenuFragment.startRetroActivity(
					retro,
					null,
					prefs.getString("libretro_path", ctx.getApplicationInfo().dataDir + "/cores/"),
					UserPreferences.getDefaultConfigPath(ctx),
					Settings.Secure.getString(ctx.getContentResolver(), Settings.Secure.DEFAULT_INPUT_METHOD),
					 ctx.getApplicationInfo().dataDir);
			startActivity(retro);
		}
		// Load Core Preference
		else if (prefKey.equals("loadCorePref"))
		{
			CoreSelection.newInstance().show(getFragmentManager(), "core_selection");
		}
		// Load ROM Preference
		else if (prefKey.equals("loadContentPref"))
		{
			final SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(ctx);
			final String libretro_path = prefs.getString("libretro_path", ctx.getApplicationInfo().dataDir + "/cores");

			if (!new File(libretro_path).isDirectory())
			{
				final DirectoryFragment contentBrowser = DirectoryFragment.newInstance(R.string.load_content);
				contentBrowser.addDisallowedExts(".state", ".srm", ".state.auto", ".rtc");
				contentBrowser.setOnDirectoryFragmentClosedListener(this);
	
				final String startPath = prefs.getString("rgui_browser_directory", "");
				if (!startPath.isEmpty() && new File(startPath).exists())
					contentBrowser.setStartDirectory(startPath);
	
				contentBrowser.show(getFragmentManager(), "contentBrowser");
			}
			else
			{
				Toast.makeText(ctx, R.string.load_a_core_first, Toast.LENGTH_SHORT).show();
			}
		}
		else if (prefKey.equals("loadContentAutoPref"))
		{
			final SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(ctx);
			final DetectCoreDirectoryFragment contentBrowser = DetectCoreDirectoryFragment.newInstance(R.string.load_content_auto);
			contentBrowser.addDisallowedExts(".state", ".srm", ".state.auto", ".rtc");
			contentBrowser.setOnDirectoryFragmentClosedListener(this);

			final String startPath = prefs.getString("rgui_browser_directory", "");
			if (!startPath.isEmpty() && new File(startPath).exists())
				contentBrowser.setStartDirectory(startPath);

			contentBrowser.show(getFragmentManager(), "contentBrowser");
		}
		// Load Content (History) Preference
		else if (prefKey.equals("loadContentHistoryPref"))
		{
			HistorySelection.newInstance().show(getFragmentManager(), "history_selection");
		}
		// Quit RetroArch preference
		else if (prefKey.equals("quitRetroArch"))
		{
			// TODO - needs to close entire app gracefully - including
			// NativeActivity if possible
			getActivity().finish();
		}

		return true;
	}

	@Override
	public void onDirectoryFragmentClosed(String path)
	{
		final SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(ctx);

		UserPreferences.updateConfigFile(ctx);
		Toast.makeText(ctx, String.format(getString(R.string.loading_data), path), Toast.LENGTH_SHORT).show();
		Intent retro = getRetroActivity();
		MainMenuFragment.startRetroActivity(
				retro,
				path,
				prefs.getString("libretro_path", ""),
				UserPreferences.getDefaultConfigPath(ctx),
				Settings.Secure.getString(ctx.getContentResolver(), Settings.Secure.DEFAULT_INPUT_METHOD),
				ctx.getApplicationInfo().dataDir);
		startActivity(retro);
	}
	
	public static void startRetroActivity(Intent retro, String contentPath, String corePath,
			String configFilePath, String imePath, String dataDirPath)
	{
		if (contentPath != null) {
			retro.putExtra("ROM", contentPath);
		}
		retro.putExtra("LIBRETRO", corePath);
		retro.putExtra("CONFIGFILE", configFilePath);
		retro.putExtra("IME", imePath);
		retro.putExtra("DATADIR", dataDirPath);
	}
}
