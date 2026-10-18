/* Zappo — the music player's engine for the browser build (WebAssembly has no
 * threads or aplay). The same decoders, volume curve and loudness levelling
 * as on the handheld; instead of a thread writing to aplay, music_pump() runs
 * once a frame, decodes a little ahead and hands the sound to the page, which
 * plays it with Web Audio.  Included by music.c when built for the web. */

/* the page's side (web/zappo.js) */
__attribute__((import_module("zappo"), import_name("audio_open"))) void zx_audio_open(int rate, int ch);
__attribute__((import_module("zappo"), import_name("audio_push"))) void zx_audio_push(const int16_t *pcm, int frames);
__attribute__((import_module("zappo"), import_name("audio_queued_ms"))) int zx_audio_queued_ms(void);
__attribute__((import_module("zappo"), import_name("audio_pause"))) void zx_audio_pause(int paused);
__attribute__((import_module("zappo"), import_name("audio_stop"))) void zx_audio_stop(int now);

static Dec wd;
static int w_open, w_ch, w_rate, w_draining, w_lv_on, w_li, w_cut;
static uint64_t w_frames;
static double w_lv_db, w_lv_sum, w_lv_n, w_hp_in, w_hp_out;
static int w_errors;

static void web_finish(int r) {
	if (w_open) {
		dec_close(&wd);
		w_open = 0;
		zx_audio_stop(r != R_END || w_cut);
		if (w_lv_on && w_lv_n > 0 && (r == R_END || w_lv_n / (double)(w_rate ? w_rate : 44100) > 120.0)) {
			double ms = w_lv_sum / w_lv_n;
			if (ms > 1e-9) { louds_set(cur_path, 10.0 * log10(ms)); louds_save(); }
		}
	}
	/* what plays next: the same choices as the handheld's play thread */
	in_track = 0;
	if (r == R_ERROR) w_errors++;
	else w_errors = 0;
	switch (r) {
	case R_END:
	case R_ERROR:
		if (r == R_END && pause_after) { pause_after = 0; paused = 1; }
		if (r == R_ERROR && w_errors >= qn) { active = 0; break; }
		if (repeat == REPEAT_ONE && r == R_END) want_play = 1;
		else if (qpos + 1 < qn) { qpos++; want_play = 1; }
		else if (repeat == REPEAT_ALL) { qpos = 0; want_play = 1; }
		else active = 0;
		break;
	case R_NEXT: qpos = (qpos + 1) % qn; want_play = 1; break;
	case R_PREV:
		if (cur_pos < 3.0) qpos = (qpos + qn - 1) % qn;
		want_play = 1;
		break;
	case R_STOP: active = 0; paused = 0; break;
	case R_RESTART: break;
	case R_QUIT: active = 0; break;
	}
	if (!active) cur_pos = 0;
	gen++;
}

static void web_start_track(void) {
	want_play = 0;
	if (qn == 0) { active = 0; gen++; return; }
	char path[1024], t[160], a[160];
	sncpy(path, queue[order[qpos]], sizeof path);
	active = 1;
	in_track = 1;
	cmd = cmd == C_RESTART ? C_NONE : cmd;
	music_tags(path, t, sizeof t, a, sizeof a);
	sncpy(cur_path, path, sizeof cur_path);
	sncpy(cur_title, t, sizeof cur_title);
	sncpy(cur_artist, a, sizeof cur_artist);
	gen++;
	if (dec_open(&wd, path) != 0) {
		set_error("Can't play %s", path_base(path));
		web_finish(R_ERROR);
		return;
	}
	w_open = 1;
	w_ch = wd.ch > 2 ? 2 : wd.ch;
	w_rate = wd.rate;
	w_frames = 0;
	w_draining = 0;
	w_cut = 0;
	cur_len = wd.len;
	cur_pos = 0;
	seek_to = -1;
	if (start_at > 0) { seek_to = start_at; cur_pos = start_at; }
	start_at = -1;
	w_lv_on = levelling;
	louds_load();
	w_li = w_lv_on ? louds_find(path) : -1;
	w_lv_db = w_li >= 0 ? lv_gain_db(louds[w_li].db) : 0.0;
	w_lv_sum = w_lv_n = w_hp_in = w_hp_out = 0;
	zx_audio_open(w_rate, w_ch);
}

/* decodes one block and hands it over; 0 at the end of the song */
static int web_block(void) {
	static int16_t buf[1152 * 2 * 2];
	int out_ch;
	long got = dec_read(&wd, buf, 1152, &out_ch);
	if (got <= 0) return 0;
	long ns = got * out_ch;
	double vgain = (double)volume * volume / 10000.0;
	if (w_lv_on) {
		double ss = 0;
		for (long i = 0; i < ns; i++) {
			double x = buf[i] / 32768.0;
			double y = 0.985 * (w_hp_out + x - w_hp_in);
			w_hp_in = x;
			w_hp_out = y;
			ss += y * y;
		}
		w_lv_sum += ss;
		w_lv_n += (double)ns;
		if (w_li < 0 && w_lv_n > 0) {
			double ms = w_lv_sum / w_lv_n;
			if (ms > 1e-9) {
				double want = lv_gain_db(10.0 * log10(ms));
				if (want > 4.0) want = 4.0;
				double secs = w_lv_n / (double)(w_rate * out_ch);
				double k = want < w_lv_db ? 0.03 : secs < 3.0 ? 0.05 : 0.004;
				w_lv_db += (want - w_lv_db) * k;
			}
		}
		vgain *= pow(10.0, w_lv_db / 20.0);
	}
	if (vgain < 0.999 || vgain > 1.001) {
		for (long i = 0; i < ns; i++) {
			double v = buf[i] * vgain;
			double a = v < 0 ? -v : v;
			if (a > 26000.0) {
				a = 26000.0 + (a - 26000.0) * 0.3;
				if (a > 32767.0) a = 32767.0;
				v = v < 0 ? -a : a;
			}
			buf[i] = (int16_t)v;
		}
	}
	zx_audio_push(buf, (int)got);
	w_frames += (uint64_t)got;
	return 1;
}

/* once a frame, from the page's animation loop */
void music_pump(void) {
	if (!w_open) {
		if (cmd == C_QUIT) return;
		if (cmd != C_NONE && cmd != C_RESTART && !in_track) cmd = C_NONE;
		if (want_play) web_start_track();
		if (!w_open) return;
	}
	int c = cmd;
	if (c != C_NONE) {
		cmd = C_NONE;
		web_finish(c == C_STOP ? R_STOP : c == C_NEXT ? R_NEXT : c == C_PREV ? R_PREV : c == C_RESTART ? R_RESTART : R_QUIT);
		if (c == C_RESTART && want_play) web_start_track();
		return;
	}
	if (seek_to >= 0) {
		double sk = seek_to;
		seek_to = -1;
		if (wd.len > 0 && sk >= wd.len - 0.5) { w_cut = 1; web_finish(R_END); return; }
		uint64_t target = (uint64_t)(sk * w_rate);
		if (dec_seek(&wd, target)) w_frames = target;
		zx_audio_stop(1);
		zx_audio_open(w_rate, w_ch);
		w_draining = 0;
		cur_pos = (double)w_frames / w_rate;
		gen++;
	}
	zx_audio_pause(paused);
	if (paused) return;
	int queued = zx_audio_queued_ms();
	while (!w_draining && queued < 450) {
		if (!web_block()) { w_draining = 1; break; }
		queued = zx_audio_queued_ms();
	}
	double heard = (double)w_frames / w_rate - queued / 1000.0;
	cur_pos = heard < 0 ? 0 : heard;
	if (w_draining && queued <= 20) web_finish(R_END);
}
