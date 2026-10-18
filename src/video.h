/* Clove — getting MP4 videos ready for the RG35XX's video player.
 *
 * The FFmpeg core in GarlicOS can only open an MP4 when its index (the
 * "moov" box) comes before the picture and sound data. HandBrake and most
 * converters put it at the end unless "Web Optimized" is ticked, so Clove
 * moves it to the front the first time a video is played (the same thing
 * "qt-faststart" does). Nothing is re-encoded, only the order of the boxes
 * changes, so the picture is exactly the same. */
#ifndef CLOVE_VIDEO_H
#define CLOVE_VIDEO_H

#include <stddef.h>

/* The video player can't open files of 2 GB or more */
#define VIDEO_MAX_BYTES 2147483647LL

/* 1 when an MP4/MOV/M4V needs its index moved to the front, 0 when it's fine
 * (or isn't an MP4 we understand: then it's left alone). */
int video_needs_prep(const char *path);
long long video_file_size(const char *path);
/* The running time in seconds from an MP4's header, 0 when unknown. */
int video_duration(const char *path);

typedef struct VideoPrep VideoPrep;

/* Starts the job; NULL with a reason in err when it can't be done. */
VideoPrep *video_prep_start(const char *path, char *err, size_t errn);
/* Does about budget_ms of work. 0 = more to do, 1 = finished (the file
 * has been replaced and *p freed), -1 = failed (err says why, *p freed). */
int video_prep_step(VideoPrep *p, int budget_ms, char *err, size_t errn);
double video_prep_progress(const VideoPrep *p); /* 0..1 */
/* Stops and removes the half-written copy; the original is untouched. */
void video_prep_cancel(VideoPrep *p);

#endif
