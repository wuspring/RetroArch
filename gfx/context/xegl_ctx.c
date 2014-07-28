/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
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

// X/EGL context. Mostly used for testing GLES code paths.
// Should be its own file as it has lots of X11 stuff baked into it as well.

#include "../../driver.h"
#include "../gfx_context.h"
#include "../gl_common.h"
#include "../gfx_common.h"
#include "x11_common.h"

#include <signal.h>
#include <stdint.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#ifndef EGL_OPENGL_ES3_BIT_KHR
#define EGL_OPENGL_ES3_BIT_KHR 0x0040
#endif

static Display *g_dpy;
static Window   g_win;
static Colormap g_cmap;
static Atom g_quit_atom;
static bool g_has_focus;
static bool g_true_full;
static unsigned g_screen;

static XIM g_xim;
static XIC g_xic;

static bool g_use_hw_ctx;
static EGLContext g_egl_hw_ctx;
static EGLContext g_egl_ctx;
static EGLSurface g_egl_surf;
static EGLDisplay g_egl_dpy;
static EGLConfig g_config;

static XF86VidModeModeInfo g_desktop_mode;
static bool g_should_reset_mode;

static volatile sig_atomic_t g_quit;
static bool g_inited;
static unsigned g_interval;
static enum gfx_ctx_api g_api;
static unsigned g_major;
static unsigned g_minor;

static void sighandler(int sig)
{
   (void)sig;
   g_quit = 1;
}

static Bool egl_wait_notify(Display *d, XEvent *e, char *arg)
{
   (void)d;
   (void)e;
   return e->type == MapNotify && e->xmap.window == g_win;
}

static int nul_handler(Display *dpy, XErrorEvent *event)
{
   (void)dpy;
   (void)event;
   return 0;
}

static void gfx_ctx_get_video_size(void *data, unsigned *width, unsigned *height);
static void gfx_ctx_destroy(void *data);

static void egl_report_error(void)
{
   EGLint error = eglGetError();
   const char *str = NULL;
   switch (error)
   {
      case EGL_SUCCESS:
         str = "EGL_SUCCESS";
         break;

      case EGL_BAD_DISPLAY:
         str = "EGL_BAD_DISPLAY";
         break;

      case EGL_BAD_SURFACE:
         str = "EGL_BAD_SURFACE";
         break;

      case EGL_BAD_CONTEXT:
         str = "EGL_BAD_CONTEXT";
         break;

      default:
         str = "Unknown";
         break;
   }

   RARCH_ERR("[X/EGL]: #0x%x, %s\n", (unsigned)error, str);
}

static void gfx_ctx_swap_interval(void *data, unsigned interval)
{
   (void)data;
   g_interval = interval;
   if (g_egl_dpy && eglGetCurrentContext())
   {
      RARCH_LOG("[X/EGL]: eglSwapInterval(%u)\n", g_interval);
      if (!eglSwapInterval(g_egl_dpy, g_interval))
      {
         RARCH_ERR("[X/EGL]: eglSwapInterval() failed.\n");
         egl_report_error();
      }
   }
}

static void gfx_ctx_check_window(void *data, bool *quit,
      bool *resize, unsigned *width, unsigned *height, unsigned frame_count)
{
   (void)frame_count;

   unsigned new_width = *width, new_height = *height;
   gfx_ctx_get_video_size(data, &new_width, &new_height);

   if (new_width != *width || new_height != *height)
   {
      *resize = true;
      *width  = new_width;
      *height = new_height;
   }

   XEvent event;
   while (XPending(g_dpy))
   {
      // Can get events from older windows. Check this.
      XNextEvent(g_dpy, &event);
      bool filter = XFilterEvent(&event, g_win);

      switch (event.type)
      {
         case ClientMessage:
            if (event.xclient.window == g_win && (Atom)event.xclient.data.l[0] == g_quit_atom)
               g_quit = true;
            break;

         case DestroyNotify:
            if (event.xdestroywindow.window == g_win)
               g_quit = true;
            break;

         case MapNotify:
            if (event.xmap.window == g_win)
               g_has_focus = true;
            break;

         case UnmapNotify:
            if (event.xunmap.window == g_win)
               g_has_focus = false;
            break;

         case KeyPress:
         case KeyRelease:
            if (event.xkey.window == g_win)
               x11_handle_key_event(&event, g_xic, filter);
            break;
      }
   }

   *quit = g_quit;
}

static void gfx_ctx_swap_buffers(void *data)
{
   (void)data;
   eglSwapBuffers(g_egl_dpy, g_egl_surf);
}

static void gfx_ctx_set_resize(void *data, unsigned width, unsigned height)
{
   (void)data;
   (void)width;
   (void)height;
}

static void gfx_ctx_update_window_title(void *data)
{
   (void)data;
   char buf[128], buf_fps[128];
   bool fps_draw = g_settings.fps_show;
   if (gfx_get_fps(buf, sizeof(buf), fps_draw ? buf_fps : NULL, sizeof(buf_fps)))
      XStoreName(g_dpy, g_win, buf);

   if (fps_draw)
      msg_queue_push(g_extern.msg_queue, buf_fps, 1, 1);
}

static void gfx_ctx_get_video_size(void *data, unsigned *width, unsigned *height)
{
   (void)data;
   if (!g_dpy || g_win == None)
   {
      Display *dpy = XOpenDisplay(NULL);
      if (dpy)
      {
         int screen = DefaultScreen(dpy);
         *width  = DisplayWidth(dpy, screen);
         *height = DisplayHeight(dpy, screen);
         XCloseDisplay(dpy);
      }
      else
      {
         *width  = 0;
         *height = 0;
      }
   }
   else
   {
      XWindowAttributes target;
      XGetWindowAttributes(g_dpy, g_win, &target);

      *width  = target.width;
      *height = target.height;
   }
}

static bool gfx_ctx_init(void *data)
{
   if (g_inited)
      return false;

   XInitThreads();

#define EGL_ATTRIBS_BASE \
   EGL_SURFACE_TYPE,    EGL_WINDOW_BIT, \
   EGL_RED_SIZE,        1, \
   EGL_GREEN_SIZE,      1, \
   EGL_BLUE_SIZE,       1, \
   EGL_ALPHA_SIZE,      0, \
   EGL_DEPTH_SIZE,      0

   static const EGLint egl_attribs_gl[] = {
      EGL_ATTRIBS_BASE,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_NONE,
   };

   static const EGLint egl_attribs_gles[] = {
      EGL_ATTRIBS_BASE,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
      EGL_NONE,
   };

#ifdef EGL_KHR_create_context
   static const EGLint egl_attribs_gles3[] = {
      EGL_ATTRIBS_BASE,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
      EGL_NONE,
   };
#endif

   static const EGLint egl_attribs_vg[] = {
      EGL_ATTRIBS_BASE,
      EGL_RENDERABLE_TYPE, EGL_OPENVG_BIT,
      EGL_NONE,
   };

   const EGLint *attrib_ptr;
   switch (g_api)
   {
      case GFX_CTX_OPENGL_API:
         attrib_ptr = egl_attribs_gl;
         break;
      case GFX_CTX_OPENGL_ES_API:
#ifdef EGL_KHR_create_context
         if (g_major >= 3)
            attrib_ptr = egl_attribs_gles3;
         else
#endif
            attrib_ptr = egl_attribs_gles;
         break;
      case GFX_CTX_OPENVG_API:
         attrib_ptr = egl_attribs_vg;
         break;
      default:
         attrib_ptr = NULL;
   }

   g_quit = 0;

   // Keep one g_dpy alive the entire process lifetime.
   // This is necessary for nVidia's EGL implementation for now.
   if (!g_dpy)
   {
      g_dpy = XOpenDisplay(NULL);
      if (!g_dpy)
         goto error;
   }

   g_egl_dpy = eglGetDisplay((EGLNativeDisplayType)g_dpy);
   if (g_egl_dpy == EGL_NO_DISPLAY)
   {
      RARCH_ERR("[X/EGL]: EGL display not available.\n");
      egl_report_error();
      goto error;
   }

   EGLint egl_major, egl_minor;
   if (!eglInitialize(g_egl_dpy, &egl_major, &egl_minor))
   {
      RARCH_ERR("[X/EGL]: Unable to initialize EGL.\n");
      egl_report_error();
      goto error;
   }

   RARCH_LOG("[X/EGL]: EGL version: %d.%d\n", egl_major, egl_minor);

   EGLint num_configs;
   if (!eglChooseConfig(g_egl_dpy, attrib_ptr, &g_config, 1, &num_configs))
   {
      RARCH_ERR("[X/EGL]: eglChooseConfig failed with 0x%x.\n", eglGetError());
      goto error;
   }

   if (num_configs == 0 || !g_config)
   {
      RARCH_ERR("[X/EGL]: No EGL configurations available.\n");
      goto error;
   }

   return true;

error:
   gfx_ctx_destroy(data);
   return false;
}

static EGLint *egl_fill_attribs(EGLint *attr)
{
   switch (g_api)
   {
#ifdef EGL_KHR_create_context
      case GFX_CTX_OPENGL_API:
      {
         unsigned version = g_major * 1000 + g_minor;
         bool core = version >= 3001;
#ifdef GL_DEBUG
         bool debug = true;
#else
         bool debug = g_extern.system.hw_render_callback.debug_context;
#endif

         if (core)
         {
            *attr++ = EGL_CONTEXT_MAJOR_VERSION_KHR;
            *attr++ = g_major;
            *attr++ = EGL_CONTEXT_MINOR_VERSION_KHR;
            *attr++ = g_minor;
            // Technically, we don't have core/compat until 3.2.
            // Version 3.1 is either compat or not depending on GL_ARB_compatibility.
            if (version >= 3002)
            {
               *attr++ = EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR;
               *attr++ = EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR;
            }
         }

         if (debug)
         {
            *attr++ = EGL_CONTEXT_FLAGS_KHR;
            *attr++ = EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR;
         }

         break;
      }
#endif

      case GFX_CTX_OPENGL_ES_API:
         *attr++ = EGL_CONTEXT_CLIENT_VERSION; // Same as EGL_CONTEXT_MAJOR_VERSION
         *attr++ = g_major ? (EGLint)g_major : 2;
#ifdef EGL_KHR_create_context
         if (g_minor > 0)
         {
            *attr++ = EGL_CONTEXT_MINOR_VERSION_KHR;
            *attr++ = g_minor;
         }
#endif
         break;

      default:
         break;
   }

   *attr = EGL_NONE;
   return attr;
}

static bool gfx_ctx_set_video_mode(void *data,
      unsigned width, unsigned height,
      bool fullscreen)
{
   struct sigaction sa = {{0}};
   sa.sa_handler = sighandler;
   sa.sa_flags   = SA_RESTART;
   sigemptyset(&sa.sa_mask);
   sigaction(SIGINT, &sa, NULL);
   sigaction(SIGTERM, &sa, NULL);

   XVisualInfo temp = {0};
   XSetWindowAttributes swa = {0};
   XVisualInfo *vi = NULL;
   bool windowed_full = g_settings.video.windowed_fullscreen;
   bool true_full = false;
   int x_off = 0;
   int y_off = 0;

   int (*old_handler)(Display*, XErrorEvent*) = NULL;

   EGLint egl_attribs[16];
   EGLint *attr = egl_attribs;

   attr = egl_fill_attribs(attr);

   EGLint vid;
   if (!eglGetConfigAttrib(g_egl_dpy, g_config, EGL_NATIVE_VISUAL_ID, &vid))
      goto error;

   temp.visualid = vid;

   EGLint num_visuals;
   vi = XGetVisualInfo(g_dpy, VisualIDMask, &temp, &num_visuals);
   if (!vi)
      goto error;

   swa.colormap = g_cmap = XCreateColormap(g_dpy, RootWindow(g_dpy, vi->screen),
         vi->visual, AllocNone);
   swa.event_mask = StructureNotifyMask | KeyPressMask | KeyReleaseMask;
   swa.override_redirect = fullscreen ? True : False;

   if (fullscreen && !windowed_full)
   {
      if (x11_enter_fullscreen(g_dpy, width, height, &g_desktop_mode))
      {
         g_should_reset_mode = true;
         true_full = true;
      }
      else
         RARCH_ERR("[X/EGL]: Entering true fullscreen failed. Will attempt windowed mode.\n");
   }

   if (g_settings.video.monitor_index)
      g_screen = g_settings.video.monitor_index - 1;

#ifdef HAVE_XINERAMA
   if (fullscreen || g_screen != 0)
   {
      unsigned new_width  = width;
      unsigned new_height = height;
      if (x11_get_xinerama_coord(g_dpy, g_screen, &x_off, &y_off, &new_width, &new_height))
         RARCH_LOG("[X/EGL]: Using Xinerama on screen #%u.\n", g_screen);
      else
         RARCH_LOG("[X/EGL]: Xinerama is not active on screen.\n");

      if (fullscreen)
      {
         width  = new_width;
         height = new_height;
      }
   }
#endif

   RARCH_LOG("[X/EGL]: X = %d, Y = %d, W = %u, H = %u.\n",
         x_off, y_off, width, height);

   g_win = XCreateWindow(g_dpy, RootWindow(g_dpy, vi->screen),
         x_off, y_off, width, height, 0,
         vi->depth, InputOutput, vi->visual, 
         CWBorderPixel | CWColormap | CWEventMask | (true_full ? CWOverrideRedirect : 0), &swa);
   XSetWindowBackground(g_dpy, g_win, 0);

   g_egl_ctx = eglCreateContext(g_egl_dpy, g_config, EGL_NO_CONTEXT,
         attr != egl_attribs ? egl_attribs : NULL);

   RARCH_LOG("[X/EGL]: Created context: %p.\n", (void*)g_egl_ctx);
   if (g_egl_ctx == EGL_NO_CONTEXT)
      goto error;

   if (g_use_hw_ctx)
   {
      g_egl_hw_ctx = eglCreateContext(g_egl_dpy, g_config, g_egl_ctx,
            attr != egl_attribs ? egl_attribs : NULL);
      RARCH_LOG("[X/EGL]: Created shared context: %p.\n", (void*)g_egl_hw_ctx);

      if (g_egl_hw_ctx == EGL_NO_CONTEXT)
         goto error;
   }

   g_egl_surf = eglCreateWindowSurface(g_egl_dpy, g_config, (EGLNativeWindowType)g_win, NULL);
   if (!g_egl_surf)
      goto error;

   if (!eglMakeCurrent(g_egl_dpy, g_egl_surf, g_egl_surf, g_egl_ctx))
      goto error;

   RARCH_LOG("[X/EGL]: Current context: %p.\n", (void*)eglGetCurrentContext());

   x11_set_window_attr(g_dpy, g_win);

   if (fullscreen)
      x11_show_mouse(g_dpy, g_win, false);

   if (true_full)
   {
      RARCH_LOG("[X/EGL]: Using true fullscreen.\n");
      XMapRaised(g_dpy, g_win);
   }
   else if (fullscreen) // We attempted true fullscreen, but failed. Attempt using windowed fullscreen.
   {
      XMapRaised(g_dpy, g_win);
      RARCH_LOG("[X/EGL]: Using windowed fullscreen.\n");
      // We have to move the window to the screen we want to go fullscreen on first.
      // x_off and y_off usually get ignored in XCreateWindow().
      x11_move_window(g_dpy, g_win, x_off, y_off, width, height);
      x11_windowed_fullscreen(g_dpy, g_win);
   }
   else
   {
      XMapWindow(g_dpy, g_win);
      // If we want to map the window on a different screen, we'll have to do it by force.
      // Otherwise, we should try to let the window manager sort it out.
      // x_off and y_off usually get ignored in XCreateWindow().
      if (g_screen)
         x11_move_window(g_dpy, g_win, x_off, y_off, width, height);
   }

   XEvent event;
   XIfEvent(g_dpy, &event, egl_wait_notify, NULL);

   g_quit_atom = XInternAtom(g_dpy, "WM_DELETE_WINDOW", False);
   if (g_quit_atom)
      XSetWMProtocols(g_dpy, g_win, &g_quit_atom, 1);

   gfx_ctx_swap_interval(data, g_interval);

   // This can blow up on some drivers. It's not fatal, so override errors for this call.
   old_handler = XSetErrorHandler(nul_handler);
   XSetInputFocus(g_dpy, g_win, RevertToNone, CurrentTime);
   XSync(g_dpy, False);
   XSetErrorHandler(old_handler);

   XFree(vi);
   g_has_focus = true;
   g_inited    = true;

   if (!x11_create_input_context(g_dpy, g_win, &g_xim, &g_xic))
      goto error;

   driver.display_type  = RARCH_DISPLAY_X11;
   driver.video_display = (uintptr_t)g_dpy;
   driver.video_window  = (uintptr_t)g_win;
   g_true_full = true_full;

   return true;

error:
   if (vi)
      XFree(vi);

   gfx_ctx_destroy(data);
   return false;
}

static void gfx_ctx_destroy(void *data)
{
   (void)data;
   x11_destroy_input_context(&g_xim, &g_xic);
   if (g_egl_dpy)
   {
      if (g_egl_ctx)
      {
         eglMakeCurrent(g_egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
         eglDestroyContext(g_egl_dpy, g_egl_ctx);
      }

      if (g_egl_hw_ctx)
         eglDestroyContext(g_egl_dpy, g_egl_hw_ctx);

      if (g_egl_surf)
         eglDestroySurface(g_egl_dpy, g_egl_surf);
      eglTerminate(g_egl_dpy);
   }

   g_egl_ctx     = NULL;
   g_egl_hw_ctx  = NULL;
   g_egl_surf    = NULL;
   g_egl_dpy     = NULL;
   g_config      = 0;

   if (g_win)
   {
      // Save last used monitor for later.
#ifdef HAVE_XINERAMA
      XWindowAttributes target;
      Window child;

      int x = 0, y = 0;
      XGetWindowAttributes(g_dpy, g_win, &target);
      XTranslateCoordinates(g_dpy, g_win, RootWindow(g_dpy, DefaultScreen(g_dpy)),
            target.x, target.y, &x, &y, &child);

      g_screen = x11_get_xinerama_monitor(g_dpy, x, y,
            target.width, target.height);

      RARCH_LOG("[X/EGL]: Saved monitor #%u.\n", g_screen);
#endif

      XUnmapWindow(g_dpy, g_win);
      XDestroyWindow(g_dpy, g_win);
      g_win = None;
   }

   if (g_cmap)
   {
      XFreeColormap(g_dpy, g_cmap);
      g_cmap = None;
   }

   if (g_should_reset_mode)
   {
      x11_exit_fullscreen(g_dpy, &g_desktop_mode);
      g_should_reset_mode = false;
   }

   // Do not close g_dpy. We'll keep one for the entire application lifecycle to work-around nVidia EGL limitations.
   g_inited = false;
}

static void gfx_ctx_input_driver(void *data, const input_driver_t **input, void **input_data)
{
   (void)data;
   void *xinput = input_x.init();
   *input       = xinput ? &input_x : NULL;
   *input_data  = xinput;
}

static bool gfx_ctx_has_focus(void *data)
{
   (void)data;
   if (!g_inited)
      return false;

   Window win;
   int rev;
   XGetInputFocus(g_dpy, &win, &rev);

   return (win == g_win && g_has_focus) || g_true_full;
}

static gfx_ctx_proc_t gfx_ctx_get_proc_address(const char *symbol)
{
   return eglGetProcAddress(symbol);
}

static bool gfx_ctx_bind_api(void *data, enum gfx_ctx_api api, unsigned major, unsigned minor)
{
   (void)data;
   g_major = major;
   g_minor = minor;
   g_api = api;
   switch (api)
   {
      case GFX_CTX_OPENGL_API:
#ifndef EGL_KHR_create_context
         if ((major * 1000 + minor) >= 3001)
            return false;
#endif
         return eglBindAPI(EGL_OPENGL_API);
      case GFX_CTX_OPENGL_ES_API:
#ifndef EGL_KHR_create_context
         if (major >= 3)
            return false;
#endif
         return eglBindAPI(EGL_OPENGL_ES_API);
      case GFX_CTX_OPENVG_API:
         return eglBindAPI(EGL_OPENVG_API);
      default:
         return false;
   }
}

static void gfx_ctx_show_mouse(void *data, bool state)
{
   (void)data;
   x11_show_mouse(g_dpy, g_win, state);
}

static void gfx_ctx_bind_hw_render(void *data, bool enable)
{
   (void)data;
   g_use_hw_ctx = enable;
   if (g_egl_dpy && g_egl_surf)
      eglMakeCurrent(g_egl_dpy, g_egl_surf, g_egl_surf, enable ? g_egl_hw_ctx : g_egl_ctx);
}

const gfx_ctx_driver_t gfx_ctx_x_egl = {
   gfx_ctx_init,
   gfx_ctx_destroy,
   gfx_ctx_bind_api,
   gfx_ctx_swap_interval,
   gfx_ctx_set_video_mode,
   gfx_ctx_get_video_size,
   NULL,
   gfx_ctx_update_window_title,
   gfx_ctx_check_window,
   gfx_ctx_set_resize,
   gfx_ctx_has_focus,
   gfx_ctx_swap_buffers,
   gfx_ctx_input_driver,
   gfx_ctx_get_proc_address,
   NULL,
   NULL,
   gfx_ctx_show_mouse,
   "x-egl",
   gfx_ctx_bind_hw_render,
};

