#include "vid_cmd.h"

static VidCmd g_vid_cmd_queue[VID_CMD_QUEUE_CAP];
static uint32_t g_vid_cmd_head;
static uint32_t g_vid_cmd_tail;
static uint32_t g_vid_cmd_count;

void Vid_cmd_queue_reset(void)
{
	g_vid_cmd_head = 0;
	g_vid_cmd_tail = 0;
	g_vid_cmd_count = 0;
}

bool Vid_cmd_push(VidCmd cmd)
{
	if(g_vid_cmd_count >= VID_CMD_QUEUE_CAP)
		return false;

	g_vid_cmd_queue[g_vid_cmd_tail] = cmd;
	g_vid_cmd_tail = (g_vid_cmd_tail + 1) % VID_CMD_QUEUE_CAP;
	g_vid_cmd_count++;
	return true;
}

bool Vid_cmd_pop(VidCmd *out)
{
	if(!out || g_vid_cmd_count == 0)
		return false;

	*out = g_vid_cmd_queue[g_vid_cmd_head];
	g_vid_cmd_head = (g_vid_cmd_head + 1) % VID_CMD_QUEUE_CAP;
	g_vid_cmd_count--;
	return true;
}

uint32_t Vid_cmd_count(void)
{
	return g_vid_cmd_count;
}
