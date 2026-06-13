#define _XOPEN_SOURCE 500 /* strdup */
#include "main.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <deadbeef/deadbeef.h>

#include "chldinit.h"
#include "chldproc.h"
#include "fmt.h"
#include "shm.h"
#include "../shmdata.h"

DB_functions_t *deadbeef;
struct Shm     *shm;
static char     shmname[sizeof(SHM_FILENAME_BASE) + 16];

/* global list of created instances. */
/* this is used instead of streamer_get_dsp_chain() because that
   function can't be used safely - it assumes you have streamer_lock
   which isn't exposed in the api. */
static struct {
	struct Ddw     *head;
	struct Ddw     *tail;
	size_t          count;
	pthread_mutex_t lk;
} g_instances = {
	.lk = PTHREAD_MUTEX_INITIALIZER,
};

static void instancelist_add(struct Ddw *plugin)
{
	pthread_mutex_lock(&g_instances.lk);
	if (g_instances.count++)
	{
		plugin->prev = g_instances.tail;
		plugin->prev->next = plugin;
		g_instances.tail = plugin;
	}
	else
	{
		g_instances.head = plugin;
		g_instances.tail = plugin;
	}
	pthread_mutex_unlock(&g_instances.lk);
}

static void instancelist_remove(struct Ddw *plugin)
{
	pthread_mutex_lock(&g_instances.lk);

	if (!plugin->prev)
		g_instances.head = plugin->next;
	if (!plugin->next)
		g_instances.tail = plugin->prev;

	if (plugin->prev)
		plugin->prev->next = plugin->next;
	if (plugin->next)
		plugin->next->prev = plugin->prev;

	g_instances.count--;

	pthread_mutex_unlock(&g_instances.lk);
}

/* placeholder to use instead of an empty string in the dsp config.
   deadbeef mishandles empty strings when loading them from disk. */
#define DSPCONFIG_EMPTY_STRING "-"

/* .so entry point */
__attribute__((visibility("default")))
DB_plugin_t *dsp_winamp_load(DB_functions_t *ddb);

/* global callbacks (not associated with a dsp instance) */
static int dsp_winamp_connect(void);
static int dsp_winamp_disconnect(void);
static int dsp_winamp_message(
    unsigned int id,
    uintptr_t    ctx,
    unsigned int param1,
    unsigned int param2);

/* dsp instance callbacks */
static ddb_dsp_context_t *dsp_winamp_open(void);
static void dsp_winamp_close(ddb_dsp_context_t *ctx);
static int dsp_winamp_process(
    ddb_dsp_context_t *ctx,
    float             *samples,
    int                frames_in,
    int                maxframes,
    ddb_waveformat_t  *fmt,
    float             *ratio);
static int dsp_winamp_num_params(void);
static const char *dsp_winamp_get_param_name(int idx);
static void dsp_winamp_set_param(
    ddb_dsp_context_t* ctx,
    int                idx,
    const char        *val);
static void dsp_winamp_get_param(
    ddb_dsp_context_t *ctx,
    int                p,
    char              *str,
    int                len);

static DB_dsp_t plugindef = {
	.plugin = {
		.type = DB_PLUGIN_DSP,
		.api_vmajor = 1,
		.api_vminor = DDB_API_LEVEL,
		.id = "dsp_winamp",
		.name = "Winamp DSP",
		.descr =
		    "Adapter that allows using DSP plugins from"
		    " Winamp.",
		.copyright =
"Winamp DSP adapter for DeaDBeeF\n"
"\n"
"For third-party acknowledgements and licenses, see:\n"
"<https://github.com/huglovefan/ddb_dsp_winamp>\n"
"\n"
"Copyright (C) 2019-2026 huglovefan <https://github.com/huglovefan>\n"
"\n"
"This program is free software: you can redistribute it and/or modify\n"
"it under the terms of the GNU Lesser General Public License as\n"
"published by the Free Software Foundation, version 3.\n"
"\n"
"This program is distributed in the hope that it will be useful, but\n"
"WITHOUT ANY WARRANTY; without even the implied warranty of\n"
"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU\n"
"Lesser General Public License for more details.\n"
"\n"
"You should have received a copy of the GNU Lesser General Public\n"
"License along with this program. If not, see\n"
"<https://www.gnu.org/licenses/>.\n"
"",
		.website =
		    "https://github.com/huglovefan/ddb_dsp_winamp",
		.connect = &dsp_winamp_connect,
		.disconnect = &dsp_winamp_disconnect,
		.message = &dsp_winamp_message,
		.configdialog =
		    "property \"Host command\" entry " CONFKEY_HOSTCMD
		    " \"ddw_host.exe\";\n"
		    "property"
		    " \"Allow returning integer samples to deadbeef\""
		    " checkbox " CONFKEY_INTOUTPUT " 0;\n"
		    "",
	},
	.open = &dsp_winamp_open,
	.close = &dsp_winamp_close,
	.process = &dsp_winamp_process,
	.num_params = &dsp_winamp_num_params,
	.get_param_name = &dsp_winamp_get_param_name,
	.set_param = &dsp_winamp_set_param,
	.get_param = &dsp_winamp_get_param,
	.configdialog =
	    "property \"Plugin list\" entry 0 \"\";\n"
	    "",
};

DB_plugin_t *dsp_winamp_load(DB_functions_t *ddb)
{
	assert(ddb);
	deadbeef = ddb;

	return &plugindef.plugin;
}

static int dsp_winamp_connect(void)
{
	snprintf(shmname, sizeof(shmname),
	    "%s%d", SHM_FILENAME_BASE, getpid());

	shm = (struct Shm *)shmnew(shmname, sizeof(struct Shm));

	return 0;
}

static int dsp_winamp_disconnect(void)
{
	if (shm)
	{
		shmfree(shm, sizeof(struct Shm));
		shm = NULL;
		unlink(shmname);
	}

	pthread_mutex_destroy(&g_instances.lk);

	return 0;
}

static void shm_update_track_strings(ddb_playItem_t *track)
{
	const char *artist, *title;
	const char *path;

	/* this is meant to mirror winamp's title formatting. it works
	   like described below. the implementation matches what's
	   written here, except that the filename isn't "prettified".

	   - file has no tags: pretty filename, e.g.
	     a_b.flac   -> a b
	     a%20b.flac -> a b
	     a-b.flac   -> a-b
	     a.b.flac   -> a.b
	     a%b.flac   -> a%b

	   - file has artist and title tags:
	     %artist% - %title%

	   - file has artist but no title tag:
	     %artist% - %prettyfilename%

	   - file has title but no artist tag:
	     %title%
	*/

	path = deadbeef->pl_find_meta(track, ":URI");

	/* shouldn't happen. don't crash. */
	if (!path)
	{
		fprintf(stderr,
		    "%s(%d): !path\n",
		    __FILE__, __LINE__);
		return;
	}

	snprintf(
	    shm->track_file_path,
	    sizeof(shm->track_file_path),
	    "%s",
	    path);

	/* note: the ones with a !bang are set by vfs_curl. they're the
	   properties of the currently playing stream track, not those
	   of the stream itself. */

	artist = deadbeef->pl_find_meta(track, "!artist");
	if (!artist)
		artist = deadbeef->pl_find_meta(track, "artist");

	title = deadbeef->pl_find_meta(track, "!title");
	if (!title)
		title = deadbeef->pl_find_meta(track, "title");

	if (!title)
	{
		const char *lastslash;
		title = path;
		/* trim the full path to just the filename. */
		lastslash = NULL;
		for (const char *p = title; *p; p++)
		{
			if (*p == '/')
				lastslash = p;
		}
		if (lastslash)
			title = lastslash+1;
	}

	if (artist)
		snprintf(
		    shm->track_title,
		    sizeof(shm->track_title),
		    "%s - %s",
		    artist,
		    title);
	else
		snprintf(
		    shm->track_title,
		    sizeof(shm->track_title),
		    "%s",
		    title);
}

static void shm_update_track_misc(
	ddb_playlist_t *playlist,
	ddb_playItem_t *track)
{
	/* takes streamer_lock */
	/* !!!note!!! must not combine with pl_lock. can deadlock. */
	shm->playback_position =
	    deadbeef->streamer_get_playpos(); /* takes streamer_lock */

	deadbeef->pl_lock();

	/* takes pl_lock */
	shm->track_duration =
	    deadbeef->pl_get_item_duration(track);

	if (playlist)
	{
		/* n/a */
		shm->playlist_length =
		    deadbeef->plt_get_item_count
		    (playlist, PL_MAIN);
		/* takes pl_lock */
		shm->playlist_position =
		    deadbeef->plt_get_item_idx
		    (playlist, track, PL_MAIN);
	}

	/* takes pl_lock */
	shm->track_sample_rate =
	    deadbeef->pl_find_meta_int(track, ":SAMPLERATE", 44100);

	/* takes pl_lock */
	shm->track_bitrate =
	    deadbeef->pl_find_meta_int(track, ":BITRATE", 128);

	/* takes pl_lock */
	shm->track_channel_count =
	    deadbeef->pl_find_meta_int(track, ":CHANNELS", 2);

	deadbeef->pl_unlock();
}

static void shm_update_global_config(void)
{
#if DDB_API_LEVEL >= 11
	shm->player_shuffle =
	    (deadbeef->streamer_get_shuffle() != DDB_SHUFFLE_OFF);
	shm->player_repeat =
	    (deadbeef->streamer_get_repeat() != DDB_REPEAT_OFF);
#endif
}

static void log_trks(const char *where)
{
	ddb_playItem_t *track;
	ddb_playlist_t *playlist;
	int pos;

	playlist = NULL;
	track = deadbeef->streamer_get_playing_track_safe();

	if (!track)
	{
		fprintf(stderr, "%s (!track)\n", where);
		goto end;
	}

	playlist = deadbeef->pl_get_playlist(track);

	if (!playlist)
	{
		fprintf(stderr, "%s (!playlist)\n", where);
		goto end;
	}

	pos =
	    deadbeef->plt_get_item_idx
	    (playlist, track, PL_MAIN);

	fprintf(stderr, "%s (idx=%d)\n", where, pos);

end:

	if (track)
		deadbeef->pl_item_unref(track);

	if (playlist)
		deadbeef->plt_unref(playlist);

}

static void request_reset_all(bool is_track_change)
{
	struct Ddw *plugin;
	ddb_playItem_t *track;

	track = NULL;

	pthread_mutex_lock(&g_instances.lk);

	if (!g_instances.count)
		goto end_unlock;

	if (is_track_change)
		/* takes streamer_lock */
		track = deadbeef->streamer_get_playing_track_safe();

	for (
	    plugin = g_instances.head;
	    plugin;
	    plugin = plugin->next)
	{
		if (is_track_change)
		{
			ddb_playItem_t *intrk;

			intrk = __atomic_exchange_n(
			    &plugin->interrupted_trk,
			    track,
			    __ATOMIC_SEQ_CST);

			if (intrk != track)
			{
				/* ref for this instance. */
				if (track)
					/* atomic */
					deadbeef->pl_item_ref(track);

				/* unref old. */
				if (intrk)
					/* atomic, can take pl_lock */
					deadbeef->pl_item_unref(intrk);
			}
		}

		pthread_mutex_lock(&plugin->lk);
		child_inform_playback_interrupted(&plugin->host);
		pthread_mutex_unlock(&plugin->lk);
	}

end_unlock:

	pthread_mutex_unlock(&g_instances.lk);

	if (track)
		/* atomic, can take pl_lock */
		deadbeef->pl_item_unref(track);
}

static void track_change_finished(void)
{
	struct Ddw *plugin;

	pthread_mutex_lock(&g_instances.lk);

	for (
	    plugin = g_instances.head;
	    plugin;
	    plugin = plugin->next)
	{
		ddb_playItem_t *intrk;

		intrk = __atomic_exchange_n(
		    &plugin->interrupted_trk,
		    NULL,
		    __ATOMIC_SEQ_CST);

		if (intrk)
			/* atomic, can take pl_lock */
			deadbeef->pl_item_unref(intrk);
	}

	pthread_mutex_unlock(&g_instances.lk);
}

static int dsp_winamp_message(
	unsigned int id,
	uintptr_t    ctx,
	unsigned int param1,
	unsigned int param2)
{
	/* we pretty much only care about any of this if shm exists. */
	if (!shm)
		return 0;

	if (0)
	switch (id)
	{
	case DB_EV_NEXT:
		printf("DB_EV_NEXT\n");
		break;
	case DB_EV_PREV:
		printf("DB_EV_PREV\n");
		break;
	case DB_EV_PLAY_CURRENT:
		printf("DB_EV_PLAY_CURRENT\n");
		break;
	case DB_EV_PLAY_NUM:
		printf("DB_EV_PLAY_NUM\n");
		break;
	case DB_EV_STOP:
		printf("DB_EV_STOP\n");
		break;
	case DB_EV_PAUSE:
		printf("DB_EV_PAUSE\n");
		break;
	case DB_EV_PLAY_RANDOM:
		printf("DB_EV_PLAY_RANDOM\n");
		break;
	case DB_EV_TERMINATE:
		printf("DB_EV_TERMINATE\n");
		break;
	case DB_EV_REINIT_SOUND:
		printf("DB_EV_REINIT_SOUND\n");
		break;
	case DB_EV_CONFIGCHANGED:
		printf("DB_EV_CONFIGCHANGED\n");
		break;
	case DB_EV_TOGGLE_PAUSE:
		printf("DB_EV_TOGGLE_PAUSE\n");
		break;
	case DB_EV_ACTIVATED:
		printf("DB_EV_ACTIVATED\n");
		break;
	case DB_EV_PAUSED:
		printf("DB_EV_PAUSED\n");
		break;
	case DB_EV_PLAYLISTCHANGED:
		printf("DB_EV_PLAYLISTCHANGED\n");
		break;
	case DB_EV_VOLUMECHANGED:
		printf("DB_EV_VOLUMECHANGED\n");
		break;
	case DB_EV_OUTPUTCHANGED:
		printf("DB_EV_OUTPUTCHANGED\n");
		break;
	case DB_EV_PLAYLISTSWITCHED:
		printf("DB_EV_PLAYLISTSWITCHED\n");
		break;
	case DB_EV_SEEK:
		printf("DB_EV_SEEK\n");
		break;
	case DB_EV_ACTIONSCHANGED:
		printf("DB_EV_ACTIONSCHANGED\n");
		break;
	case DB_EV_DSPCHAINCHANGED:
		printf("DB_EV_DSPCHAINCHANGED\n");
		break;
	case DB_EV_SELCHANGED:
		printf("DB_EV_SELCHANGED\n");
		break;
	case DB_EV_PLUGINSLOADED:
		printf("DB_EV_PLUGINSLOADED\n");
		break;
	case DB_EV_FOCUS_SELECTION:
		printf("DB_EV_FOCUS_SELECTION\n");
		break;
	case DB_EV_PLAYBACK_STATE_DID_CHANGE:
		printf("DB_EV_PLAYBACK_STATE_DID_CHANGE p1=%u\n", param1);
		break;
	case DB_EV_PLAY_NEXT_ALBUM:
		printf("DB_EV_PLAY_NEXT_ALBUM\n");
		break;
	case DB_EV_PLAY_PREV_ALBUM:
		printf("DB_EV_PLAY_PREV_ALBUM\n");
		break;
	case DB_EV_PLAY_RANDOM_ALBUM:
		printf("DB_EV_PLAY_RANDOM_ALBUM\n");
		break;

	case DB_EV_SONGCHANGED:
		printf("DB_EV_SONGCHANGED\n");
		break;
	case DB_EV_SONGSTARTED:
		printf("DB_EV_SONGSTARTED\n");
		break;
	case DB_EV_SONGFINISHED:
		printf("DB_EV_SONGFINISHED\n");
		break;
	case DB_EV_TRACKINFOCHANGED:
		printf("DB_EV_TRACKINFOCHANGED\n");
		break;
	case DB_EV_SEEKED:
		printf("DB_EV_SEEKED\n");
		break;
	case DB_EV_TRACKFOCUSCURRENT:
		printf("DB_EV_TRACKFOCUSCURRENT\n");
		break;
	case DB_EV_CURSOR_MOVED:
		printf("DB_EV_CURSOR_MOVED\n");
		break;

	default:
		printf("%u\n", id);
	}
	//~ if (ctx || param1 || param2)
		//~ printf(" -> ctx=%p p1=%u p2=%u\n", (void *)ctx, param1, param2);

	switch (id)
	{
	/* 1 */
	case DB_EV_NEXT:
		request_reset_all(true);
		break;

	/* 2 */
	case DB_EV_PREV:
		request_reset_all(true);
		break;

	/* 4 */
	case DB_EV_PLAY_NUM:
		request_reset_all(true);
		break;

	/* 5 */
	case DB_EV_STOP:
	{
		bool info;

		info = (shm->playback_state == SHM_PLSTATE_PLAYING);
		shm->playback_state = SHM_PLSTATE_STOPPED;
		if (info)
			request_reset_all(false);
		break;
	}

	/* 11 */
	case DB_EV_CONFIGCHANGED:
	{
		struct Ddw *plugin;

		shm_update_global_config();

		pthread_mutex_lock(&g_instances.lk);
		if (g_instances.count)
		{
			const char *host_cmd;

			deadbeef->conf_lock();
			host_cmd = deadbeef->conf_get_str_fast(
			    CONFKEY_HOSTCMD,
			    "ddw_host.exe");
			for (
			    plugin = g_instances.head;
			    plugin;
			    plugin = plugin->next)
			{
				if (plugin->host.host_cmd
				    && strcmp(host_cmd, plugin->host.host_cmd))
					plugin->host.config_changed = true;
			}
			deadbeef->conf_unlock();
		}
		pthread_mutex_unlock(&g_instances.lk);

		break;
	}

	/* 14 */
	case DB_EV_PAUSED:
	{
		bool info;

		info = false;
		if (param1)
			info = (shm->playback_state == SHM_PLSTATE_PLAYING),
			shm->playback_state = SHM_PLSTATE_PAUSED;
		else
			shm->playback_state = SHM_PLSTATE_PLAYING;
		//~ if (info)
			//~ request_reset_all(false);
		break;
	}

	/* 15 */
	case DB_EV_PLAYLISTCHANGED:
	{
		enum ddb_playlist_change_t which;

		which = (enum ddb_playlist_change_t)param1;

		/* note: this is required so that we catch edits to the
		   playing track's title metadata. the other event seems
		   to be sent too early. */
		if (which == DDB_PLAYLIST_CHANGE_CONTENT)
			goto check_trk;

		break;
	}

	/* 1001 */
	case DB_EV_SONGSTARTED:
		shm->playback_state = SHM_PLSTATE_PLAYING;
		track_change_finished();
		break;

	/* 1004 */
	case DB_EV_TRACKINFOCHANGED:
check_trk:
	{
		ddb_event_track_t *info;
		ddb_playlist_t *playlist;
		ddb_playItem_t *track;

		if (id == DB_EV_TRACKINFOCHANGED)
		{
			assert(ctx);
			info = (ddb_event_track_t *)ctx;
		}
		else
			info = NULL;

		/* clicked a different track, no change to what's
		   actually playing. */
		if (param1 == DDB_PLAYLIST_CHANGE_SELECTION)
			break;

		/* idk, but this filters out some extraneous events. */
		if (id == DB_EV_TRACKINFOCHANGED && !info->track)
			break;

		track = deadbeef->streamer_get_playing_track_safe();

		if (!track)
			break;

		/* event is for a different track. */
		if (id == DB_EV_TRACKINFOCHANGED
		    && info->track != track)
		{
			deadbeef->pl_item_unref(track);
			break;
		}

		playlist = deadbeef->pl_get_playlist(track);

		/* this function does its own locking. */
		shm_update_track_misc(playlist, track);
		/* this function uses pl_find_meta which needs pl_lock.
		   */
		deadbeef->pl_lock();
		shm_update_track_strings(track);
		deadbeef->pl_unlock();

		deadbeef->pl_item_unref(track);

		if (playlist)
			deadbeef->plt_unref(playlist);

		//~ printf("d: set OK\n");
		//~ printf(" -> id=%s\n", (id == DB_EV_TRACKINFOCHANGED)
		    //~ ? "DB_EV_TRACKINFOCHANGED"
		    //~ : "DB_EV_PLAYLISTCHANGED");
		//~ printf(" -> ctx=%p\n", (void *)ctx);
		//~ if (info)
			//~ printf(" -> .track=%p\n", info->track);
		//~ printf(" -> p1=%u\n", param1);
		//~ printf(" -> p2=%u\n", param2);
		//~ printf(" -> track=%p\n", track);

		break;
	}

	/* 1005 */
	case DB_EV_SEEKED:
		request_reset_all(false);
		break;
	}

	return 0;
}

static ddb_dsp_context_t *dsp_winamp_open(void)
{
	struct Ddw *plugin;

	plugin = (struct Ddw *)malloc(sizeof(struct Ddw));
	if (!plugin)
		return NULL;

	*plugin = DDW_INIT;

	plugin->ctx.plugin = &plugindef;
	plugin->ctx.enabled = 1;

	plugin->host.pl = plugin;

	instancelist_add(plugin);

	return &plugin->ctx;
}

static void dsp_winamp_close(ddb_dsp_context_t *ctx)
{
	struct Ddw *plugin;

	plugin = (struct Ddw *)ctx;

	instancelist_remove(plugin);

	child_stop(&plugin->host);

	if (plugin->interrupted_trk)
	{
		deadbeef->pl_item_unref(plugin->interrupted_trk);
		plugin->interrupted_trk = NULL;
	}

	pthread_mutex_destroy(&plugin->lk);

	free(plugin);
}

static SFMT_REQ get_output_sample_format(struct Ddw *plugin)
{
	/* if we're the last dsp, we can potentially return samples in
	   integer format. deadbeef is fine with that, but other dsps
	   probably wouldn't be. */
	/* TODO: does deadbeef not convert it? verify. */
	if (!plugin->ctx.next
	    && deadbeef->conf_get_int(CONFKEY_INTOUTPUT, 0))
		return SFMT_REQ_ANY_32BIT;

	return SFMT_REQ_ONLY_F32;
}

static bool is_intrk(struct Ddw *plugin)
{
	ddb_playItem_t *intrk;
	ddb_playItem_t *current;
	bool same;

	intrk = __atomic_load_n(
	    &plugin->interrupted_trk,
	    __ATOMIC_SEQ_CST);
	if (!intrk)
		return false;

	current = deadbeef->streamer_get_playing_track_safe();
	if (!current)
		return false;

	same = (current == plugin->interrupted_trk);

	deadbeef->pl_item_unref(current);
	current = NULL;

	if (same)
		return true;

	return false;
}

static int dsp_winamp_process(
	ddb_dsp_context_t *ctx,
	float             *samples,
	int                frames_in,
	int                maxframes,
	ddb_waveformat_t  *fmt,
	float             *ratio)
{
	size_t sampbuflen;
	size_t sampbuflen_max;
	struct Ddw *plugin;
	unsigned int frames_out;
	SFMT_REQ reqfmt;
	bool ok;

	plugin = (struct Ddw *)ctx;

	if (is_intrk(plugin))
		return 0;

	sampbuflen = waveformat_frame_bytes_n(fmt, frames_in);

	/* maximally large output buffer. */
	/* note: this value is potentially wrong if an earlier dsp
	   changed the channel count. */
	/* sample size is hardcoded to float because deadbeef assumes it
	   when calculating the value. */
	sampbuflen_max = (maxframes*(32/8)*fmt->channels);

	reqfmt = get_output_sample_format(plugin);

	shm->playback_position = deadbeef->streamer_get_playpos();

	pthread_mutex_lock(&plugin->lk);
	ok = child_process_samples(&plugin->host,
	    (char *)samples,
	    &sampbuflen,
	    sampbuflen_max,
	    fmt,
	    reqfmt);
	pthread_mutex_unlock(&plugin->lk);

	if (ok)
	{
		assert(sampbuflen <= sampbuflen_max);

		frames_out = waveformat_buf_frames(fmt, sampbuflen);
	}
	else
	{
		child_stop(&plugin->host);

		deadbeef->get_output()->pause();

		frames_out = 0;
	}

	if (reqfmt == SFMT_REQ_ANY_32BIT)
		assert(fmt->bps == 32);
	if (reqfmt == SFMT_REQ_ONLY_F32)
		assert(fmt->bps == 32 && fmt->is_float);

	if (frames_out > 0)
		*ratio = (float)frames_in / (float)frames_out;
	else
		*ratio = 0.0f;

	return frames_out;
}

static int dsp_winamp_num_params(void)
{
	return 1;
}

/* this doesn't seem to be used by deadbeef currently. */
static const char *dsp_winamp_get_param_name(int p)
{
	switch (p)
	{
	case 0:
		return "Plugin list";
	default:
		return "?";
	}
}

static void dsp_winamp_set_param(
	ddb_dsp_context_t* ctx,
	int                idx,
	const char        *val)
{
	struct Ddw *plugin;
	char *p;

	plugin = (struct Ddw *)ctx;

	switch (idx)
	{
	case 0:
		if (!plugin->dll || strcmp(val, plugin->dll))
		{
			p = strdup(val);
			if (p)
			{
				free(plugin->dll);
				plugin->dll = p;
				plugin->host.config_changed = true;
			}
		}
		break;
	}
}

static void dsp_winamp_get_param(
	ddb_dsp_context_t *ctx,
	int                p,
	char              *str,
	int                len)
{
	struct Ddw *plugin;

	plugin = (struct Ddw *)ctx;

	switch (p)
	{
	case 0:
		snprintf(str, len, "%s", plugin->dll ? plugin->dll : "");
		break;
	default:
		goto empty_string;
	}

	/* fix up empty values. */
	if (!str[0])
empty_string:
		snprintf(str, len, "%s", DSPCONFIG_EMPTY_STRING);
}
