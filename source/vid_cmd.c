#include "vid_cmd.h"

#include "system/util/err.h"
#include "system/util/log.h"
#include "system/util/sync.h"

#include <inttypes.h>

static VidCmd g_vid_cmd_queue[VID_CMD_QUEUE_CAP];
static uint32_t g_vid_cmd_head;
static uint32_t g_vid_cmd_tail;
static uint32_t g_vid_cmd_count;
static Sync_data g_vid_cmd_mutex;
static bool g_vid_cmd_mutex_ok;
static uint32_t g_vid_cmd_drop_total;

static void vid_cmd_lock(void)
{
	if(g_vid_cmd_mutex_ok)
		(void)Util_sync_lock(&g_vid_cmd_mutex, UINT64_MAX);
}

static void vid_cmd_unlock(void)
{
	if(g_vid_cmd_mutex_ok)
		(void)Util_sync_unlock(&g_vid_cmd_mutex);
}

/* Drop oldest SEEK_BAR_DRAG while queue is full; those are superseded by newer DRAG/COMMIT. */
static void vid_cmd_evict_stale_drags_at_head(void)
{
	while(g_vid_cmd_count >= VID_CMD_QUEUE_CAP
	&& g_vid_cmd_queue[g_vid_cmd_head].id == VID_CMD_SEEK_BAR_DRAG)
	{
		g_vid_cmd_head = (g_vid_cmd_head + 1) % VID_CMD_QUEUE_CAP;
		g_vid_cmd_count--;
	}
}

uint32_t Vid_cmd_init(void)
{
	uint32_t r = DEF_ERR_OTHER;

	g_vid_cmd_head = 0;
	g_vid_cmd_tail = 0;
	g_vid_cmd_count = 0;
	g_vid_cmd_drop_total = 0;

	if(g_vid_cmd_mutex_ok)
		return DEF_SUCCESS;

	DEF_LOG_RESULT_SMART(r, Util_sync_create(&g_vid_cmd_mutex, SYNC_TYPE_NON_RECURSIVE_MUTEX), (r == DEF_SUCCESS), r);
	if(r == DEF_SUCCESS)
		g_vid_cmd_mutex_ok = true;

	return r;
}

void Vid_cmd_destroy(void)
{
	vid_cmd_lock();
	g_vid_cmd_head = 0;
	g_vid_cmd_tail = 0;
	g_vid_cmd_count = 0;
	vid_cmd_unlock();

	if(g_vid_cmd_mutex_ok)
	{
		(void)Util_sync_destroy(&g_vid_cmd_mutex);
		g_vid_cmd_mutex_ok = false;
	}
}

void Vid_cmd_queue_reset(void)
{
	vid_cmd_lock();
	g_vid_cmd_head = 0;
	g_vid_cmd_tail = 0;
	g_vid_cmd_count = 0;
	vid_cmd_unlock();
}

bool Vid_cmd_push(VidCmd cmd)
{
	bool ok = false;

	vid_cmd_lock();

	if(cmd.id == VID_CMD_SEEK_BAR_DRAG || cmd.id == VID_CMD_SEEK_BAR_COMMIT)
		vid_cmd_evict_stale_drags_at_head();

	if(g_vid_cmd_count < VID_CMD_QUEUE_CAP)
	{
		g_vid_cmd_queue[g_vid_cmd_tail] = cmd;
		g_vid_cmd_tail = (g_vid_cmd_tail + 1) % VID_CMD_QUEUE_CAP;
		g_vid_cmd_count++;
		ok = true;
	}
	else
	{
		g_vid_cmd_drop_total++;
		DEF_LOG_FORMAT("Vid_cmd_push: queue full, dropped cmd id=%d (drops=%" PRIu32 ")",
			(int)cmd.id, g_vid_cmd_drop_total);
	}

	vid_cmd_unlock();
	return ok;
}

bool Vid_cmd_pop(VidCmd *out)
{
	bool ok = false;

	if(!out)
		return false;

	vid_cmd_lock();

	if(g_vid_cmd_count > 0)
	{
		*out = g_vid_cmd_queue[g_vid_cmd_head];
		g_vid_cmd_head = (g_vid_cmd_head + 1) % VID_CMD_QUEUE_CAP;
		g_vid_cmd_count--;
		ok = true;
	}

	vid_cmd_unlock();
	return ok;
}

uint32_t Vid_cmd_count(void)
{
	uint32_t n;

	vid_cmd_lock();
	n = g_vid_cmd_count;
	vid_cmd_unlock();
	return n;
}

uint32_t Vid_cmd_dropped_total(void)
{
	uint32_t n;

	vid_cmd_lock();
	n = g_vid_cmd_drop_total;
	vid_cmd_unlock();
	return n;
}
