/*
 * Minimal libretro frontend for testing BlockBox without RetroArch.
 *
 * usage: harness CORE.so CONTENT|- FRAMES SCRIPT|- OUTPREFIX DUMPFRAMES [key=value ...]
 *   SCRIPT lines:  <first_frame> <last_frame> <button>   (button held on those frames)
 *   DUMPFRAMES:    comma list of frame numbers to save as OUTPREFIX_<n>.ppm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>
#include <dlfcn.h>
#include "libretro.h"

static unsigned (*p_api_version)(void);
static void (*p_set_environment)(retro_environment_t);
static void (*p_set_video_refresh)(retro_video_refresh_t);
static void (*p_set_audio_sample)(retro_audio_sample_t);
static void (*p_set_audio_sample_batch)(retro_audio_sample_batch_t);
static void (*p_set_input_poll)(retro_input_poll_t);
static void (*p_set_input_state)(retro_input_state_t);
static void (*p_init)(void);
static void (*p_deinit)(void);
static void (*p_get_system_av_info)(struct retro_system_av_info *);
static void (*p_get_system_info)(struct retro_system_info *);
static bool (*p_load_game)(const struct retro_game_info *);
static void (*p_unload_game)(void);
static void (*p_run)(void);

static struct { char key[64]; char val[64]; int user; } opts[64];
static int nopts;

static uint16_t frame[320 * 240];
static unsigned fw, fh;
static long video_frames, dupe_frames;
static long audio_frames;
static int cur_frame;

#define MAXEV 256
static struct { int a, b, id; } ev[MAXEV];
static int nev;

static void core_log(enum retro_log_level level, const char *fmt, ...)
{
   va_list ap;
   (void)level;
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);
}

static void set_opt(const char *k, const char *v, int user)
{
   int i;
   for (i = 0; i < nopts; i++)
      if (!strcmp(opts[i].key, k))
      {
         if (user || !opts[i].user)
         {
            snprintf(opts[i].val, sizeof(opts[i].val), "%s", v);
            opts[i].user |= user;
         }
         return;
      }
   snprintf(opts[nopts].key, sizeof(opts[nopts].key), "%s", k);
   snprintf(opts[nopts].val, sizeof(opts[nopts].val), "%s", v);
   opts[nopts].user = user;
   nopts++;
}

static bool env_cb(unsigned cmd, void *data)
{
   switch (cmd)
   {
   case RETRO_ENVIRONMENT_SET_VARIABLES:
   {
      const struct retro_variable *v = (const struct retro_variable *)data;
      for (; v->key; v++)
      {
         const char *p = strstr(v->value, "; ");
         char def[64];
         size_t n;
         if (!p) continue;
         p += 2;
         n = strcspn(p, "|");
         if (n >= sizeof(def)) n = sizeof(def) - 1;
         memcpy(def, p, n);
         def[n] = 0;
         set_opt(v->key, def, 0);
      }
      return true;
   }
   case RETRO_ENVIRONMENT_GET_VARIABLE:
   {
      struct retro_variable *v = (struct retro_variable *)data;
      int i;
      for (i = 0; i < nopts; i++)
         if (!strcmp(opts[i].key, v->key))
         {
            v->value = opts[i].val;
            return true;
         }
      v->value = NULL;
      return false;
   }
   case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      *(bool *)data = false;
      return true;
   case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      return *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_RGB565;
   case RETRO_ENVIRONMENT_GET_CAN_DUPE:
      *(bool *)data = true;
      return true;
   case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
   case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
      return true;
   case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
   case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      *(const char **)data = ".";
      return true;
   case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
      ((struct retro_log_callback *)data)->log = core_log;
      return true;
   case RETRO_ENVIRONMENT_SET_GEOMETRY:
      return true;
   default:
      return false;
   }
}

static void video_refresh(const void *data, unsigned w, unsigned h, size_t pitch)
{
   unsigned y;
   if (!data)
   {
      dupe_frames++;
      return;
   }
   video_frames++;
   fw = w; fh = h;
   for (y = 0; y < h; y++)
      memcpy(frame + y * w, (const uint8_t *)data + y * pitch, w * 2);
}

static void audio_sample(int16_t l, int16_t r) { (void)l; (void)r; audio_frames++; }
static size_t audio_batch(const int16_t *d, size_t n) { (void)d; audio_frames += (long)n; return n; }
static void input_poll(void) {}

static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
   int i;
   (void)index;
   if (port != 0 || device != RETRO_DEVICE_JOYPAD)
      return 0;
   for (i = 0; i < nev; i++)
      if ((int)id == ev[i].id && cur_frame >= ev[i].a && cur_frame <= ev[i].b)
         return 1;
   return 0;
}

static int button_id(const char *s)
{
   static const struct { const char *n; int id; } map[] = {
      { "b", RETRO_DEVICE_ID_JOYPAD_B }, { "y", RETRO_DEVICE_ID_JOYPAD_Y },
      { "select", RETRO_DEVICE_ID_JOYPAD_SELECT }, { "start", RETRO_DEVICE_ID_JOYPAD_START },
      { "up", RETRO_DEVICE_ID_JOYPAD_UP }, { "down", RETRO_DEVICE_ID_JOYPAD_DOWN },
      { "left", RETRO_DEVICE_ID_JOYPAD_LEFT }, { "right", RETRO_DEVICE_ID_JOYPAD_RIGHT },
      { "a", RETRO_DEVICE_ID_JOYPAD_A }, { "x", RETRO_DEVICE_ID_JOYPAD_X },
      { "l", RETRO_DEVICE_ID_JOYPAD_L }, { "r", RETRO_DEVICE_ID_JOYPAD_R },
      { "l2", RETRO_DEVICE_ID_JOYPAD_L2 }, { "r2", RETRO_DEVICE_ID_JOYPAD_R2 },
   };
   unsigned i;
   for (i = 0; i < sizeof(map) / sizeof(map[0]); i++)
      if (!strcmp(map[i].n, s))
         return map[i].id;
   fprintf(stderr, "unknown button %s\n", s);
   exit(2);
}

static void dump_ppm(const char *prefix, int n)
{
   char path[512];
   FILE *f;
   unsigned i;
   snprintf(path, sizeof(path), "%s_%d.ppm", prefix, n);
   f = fopen(path, "wb");
   if (!f) { perror(path); return; }
   fprintf(f, "P6\n%u %u\n255\n", fw, fh);
   for (i = 0; i < fw * fh; i++)
   {
      uint16_t p = frame[i];
      unsigned char rgb[3];
      rgb[0] = (unsigned char)(((p >> 11) & 31) * 255 / 31);
      rgb[1] = (unsigned char)(((p >> 5) & 63) * 255 / 63);
      rgb[2] = (unsigned char)((p & 31) * 255 / 31);
      fwrite(rgb, 1, 3, f);
   }
   fclose(f);
}

static double now_s(void)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return ts.tv_sec + ts.tv_nsec / 1e9;
}

#define LOAD(sym, name) do { *(void **)(&sym) = dlsym(h, name); if (!sym) { fprintf(stderr, "missing %s\n", name); return 1; } } while (0)

int main(int argc, char **argv)
{
   void *h;
   int frames, i, dumps[64], ndump = 0;
   struct retro_game_info gi;
   struct retro_system_av_info av;
   struct retro_system_info si;
   double t0, t_total = 0, t_max = 0;
   char *tok, dumpbuf[512];

   if (argc < 7)
   {
      fprintf(stderr, "usage: %s core content frames script outprefix dumpframes [k=v...]\n", argv[0]);
      return 2;
   }
   h = dlopen(argv[1], RTLD_NOW);
   if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
   LOAD(p_api_version, "retro_api_version");
   LOAD(p_set_environment, "retro_set_environment");
   LOAD(p_set_video_refresh, "retro_set_video_refresh");
   LOAD(p_set_audio_sample, "retro_set_audio_sample");
   LOAD(p_set_audio_sample_batch, "retro_set_audio_sample_batch");
   LOAD(p_set_input_poll, "retro_set_input_poll");
   LOAD(p_set_input_state, "retro_set_input_state");
   LOAD(p_init, "retro_init");
   LOAD(p_deinit, "retro_deinit");
   LOAD(p_get_system_av_info, "retro_get_system_av_info");
   LOAD(p_get_system_info, "retro_get_system_info");
   LOAD(p_load_game, "retro_load_game");
   LOAD(p_unload_game, "retro_unload_game");
   LOAD(p_run, "retro_run");

   for (i = 7; i < argc; i++)
   {
      char k[64], *eq = strchr(argv[i], '=');
      if (!eq) continue;
      snprintf(k, sizeof(k), "%.*s", (int)(eq - argv[i]), argv[i]);
      set_opt(k, eq + 1, 1);
   }

   frames = atoi(argv[3]);
   if (strcmp(argv[4], "-"))
   {
      FILE *sf = fopen(argv[4], "r");
      char line[128], bn[32];
      if (!sf) { perror(argv[4]); return 1; }
      while (fgets(line, sizeof(line), sf) && nev < MAXEV)
      {
         if (line[0] == '#' || line[0] == '\n') continue;
         if (sscanf(line, "%d %d %31s", &ev[nev].a, &ev[nev].b, bn) == 3)
         {
            ev[nev].id = button_id(bn);
            nev++;
         }
      }
      fclose(sf);
   }
   snprintf(dumpbuf, sizeof(dumpbuf), "%s", argv[6]);
   for (tok = strtok(dumpbuf, ","); tok && ndump < 64; tok = strtok(NULL, ","))
      dumps[ndump++] = atoi(tok);

   p_set_environment(env_cb);
   p_set_video_refresh(video_refresh);
   p_set_audio_sample(audio_sample);
   p_set_audio_sample_batch(audio_batch);
   p_set_input_poll(input_poll);
   p_set_input_state(input_state);
   p_get_system_info(&si);
   printf("core: %s %s (api %u)\n", si.library_name, si.library_version, p_api_version());

   t0 = now_s();
   p_init();
   memset(&gi, 0, sizeof(gi));
   gi.path = argv[2];
   if (!p_load_game(strcmp(argv[2], "-") ? &gi : NULL))
   {
      fprintf(stderr, "load_game failed\n");
      return 1;
   }
   printf("init+load: %.3f s\n", now_s() - t0);
   p_get_system_av_info(&av);
   printf("av: %ux%u fps %.1f rate %.0f\n", av.geometry.base_width, av.geometry.base_height,
          av.timing.fps, av.timing.sample_rate);

   for (cur_frame = 1; cur_frame <= frames; cur_frame++)
   {
      double a = now_s(), d;
      long before = video_frames;
      p_run();
      d = now_s() - a;
      if (video_frames != before)
      {
         t_total += d;
         if (d > t_max) t_max = d;
      }
      for (i = 0; i < ndump; i++)
         if (dumps[i] == cur_frame)
            dump_ppm(argv[5], cur_frame);
   }
   printf("frames run: %d, video frames: %ld, dupes: %ld, audio frames: %ld\n",
          frames, video_frames, dupe_frames, audio_frames);
   if (video_frames)
      printf("avg ms per presented frame: %.2f, max %.2f\n", t_total / video_frames * 1000.0, t_max * 1000.0);
   t0 = now_s();
   p_unload_game();
   printf("unload (save): %.3f s\n", now_s() - t0);
   p_deinit();
   dlclose(h);
   return 0;
}
