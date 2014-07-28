/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2013-2014 - Jason Fetters
 *  Copyright (C) 2011-2014 - Daniel De Matteis
 * 
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>

#import "RetroArch_Apple.h"
#include "../../frontend/frontend.h"
#include "../../file.h"

id<RetroArch_Platform> apple_platform;

void apple_rarch_exited(void)
{
   [apple_platform unloadingCore];
}

void apple_run_core(int argc, char **argv, const char* core,
      const char* file)
{
   static char core_path[PATH_MAX], file_path[PATH_MAX],
               config_path[PATH_MAX];
    NSString *core_to_load = core ? BOXSTRING(core) : nil;
   [apple_platform loadingCore:core_to_load withFile:file];

   if (file)
      strlcpy(file_path, file, sizeof(file_path));
   if (core)
      strlcpy(core_path, core, sizeof(core_path));

   strlcpy(config_path, g_defaults.config_path, sizeof(config_path));
   if (core_info_has_custom_config(core))
      core_info_get_custom_config(core, config_path, sizeof(config_path));

   static const char* const argv_game[] = { "retroarch", "-c", config_path, "-L", core_path, file_path, 0 };
   static const char* const argv_menu[] = { "retroarch", "-c", config_path, "--menu", 0 };

   if (argc == 0)
      argc = (file && core) ? 6 : 4;
   if (!argv)
      argv = (char**)((file && core) ? argv_game : argv_menu);

   if (rarch_main(argc, argv))
   {
      char basedir[256];
      fill_pathname_basedir(basedir, file ? file : "", sizeof(basedir));
      if (file && access(basedir, R_OK | W_OK | X_OK))
         apple_display_alert("The directory containing the selected file must have write permissions. This will prevent zipped content from loading, and will cause some cores to not function.", "Warning");
      else
         apple_display_alert("Failed to load content.", "Error");

      main_exit(NULL);
   }
}
