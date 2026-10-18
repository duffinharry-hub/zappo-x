/* Clove — the music player: MP3, OGG, FLAC and WAV play inside Clove (and
 * keep playing while you browse); other audio (M4A, AAC, OPUS, ...) is handed
 * to RetroArch's FFmpeg player. Sound goes out through GarlicOS's `aplay`. */
#ifndef CLOVE_MUSIC_H
#define CLOVE_MUSIC_H

#include <stddef.h>
#include <stdint.h>

enum { MUSIC_NO, MUSIC_CLOVE, MUSIC_RETROARCH };
enum { REPEAT_OFF, REPEAT_ALL, REPEAT_ONE };

typedef struct {
	int active;          /* a track is loaded (playing or paused) */
	int paused;
	char title[160], artist[160];
	char path[1024];
	double pos, len;     /* seconds; len is 0 when unknown */
	int index, count;    /* position in the queue */
	int volume;          /* 0..100 */
	int shuffle, repeat;
	char error[160];     /* last problem, for a toast */
} MusicInfo;

/* The Music folder on the card GarlicOS runs from (created if missing). */
void music_dir(char *out, size_t n);
int music_kind(const char *path); /* MUSIC_NO / MUSIC_CLOVE / MUSIC_RETROARCH */

void music_init(int volume, int shuffle, int repeat);
void music_shutdown(void);

/* Plays paths[start], then the rest of the list. The list is copied. */
void music_play(char *const *paths, int n, int start);
void music_toggle_pause(void);
void music_stop(void);
/* Jumps `delta` seconds forward (or back, when negative) in the song. Presses
 * add up, so holding the button keeps skipping; past the end goes on to
 * whatever plays next. */
void music_seek(double delta);
/* Like music_play, but paused at `pos` seconds into paths[start]. */
void music_resume(char *const *paths, int n, int start, double pos);
/* Sleep timer "end of this song": pause when the current track finishes. */
void music_pause_after_song(int on);
int music_pausing_after_song(void);
/* Even out the loudness of different songs (on by default). */
void music_set_levelling(int on);
/* The queue and position, saved on the way out and picked up next time. */
void music_save_state(const char *file);
int music_load_state(const char *file);
void music_next(void);
void music_prev(void);
void music_set_volume(int v);
void music_set_shuffle(int on);
void music_set_repeat(int mode);

void music_info(MusicInfo *out);
int music_is_playing(void);        /* loaded and not paused */
uint32_t music_generation(void);   /* changes whenever the track or state changes */
int music_take_error(char *out, size_t n);
/* browser build only: decode a little ahead, once a frame (music_web.inc) */
void music_pump(void);

/* Length in seconds (0 when unknown). Reads only the headers. */
double music_length(const char *path);
/* An ID3v2 text frame (encoding byte + text) as UTF-8. */
void music_id3_text(const unsigned char *p, size_t n, char *out, size_t on);

/* Title / artist from the file's tags, else a tidy file name. */
void music_tags(const char *path, char *title, size_t tn, char *artist, size_t an);

#endif
