#include "system/topos_setting.h"

#include <stdlib.h>
#include <string.h>

#include "system/util/err_types.h"
#include "system/util/file.h"
#include "system/util/str.h"

#define TOPOS_MD_MAX_BYTES (uint32_t)(0x5000)

static char* s_boot_vid = NULL;
static uint32_t s_boot_vid_len = 0;
static bool s_boot_vid_pending = false;

static void Topos_md_boot_discard_stash(void)
{
	free(s_boot_vid);
	s_boot_vid = NULL;
	s_boot_vid_len = 0;
	s_boot_vid_pending = false;
}

void Topos_md_boot_stash_vid(Topos_md_bundle* b)
{
	Topos_md_boot_discard_stash();
	if(!b)
		return;
	if(b->vid_text || b->vid_len > 0)
	{
		s_boot_vid = b->vid_text;
		s_boot_vid_len = b->vid_len;
		s_boot_vid_pending = true;
		b->vid_text = NULL;
		b->vid_len = 0;
	}
}

bool Topos_md_boot_take_vid(char** vid, uint32_t* len)
{
	if(!vid || !len || !s_boot_vid_pending)
		return false;
	*vid = s_boot_vid;
	*len = s_boot_vid_len;
	s_boot_vid = NULL;
	s_boot_vid_len = 0;
	s_boot_vid_pending = false;
	return true;
}

static uint32_t topos_str_append_raw(Str_data* s, const char* p, uint32_t n)
{
	uint32_t new_length = 0;
	uint32_t result = DEF_ERR_OTHER;

	if(!Util_str_is_valid(s) || !p)
		return DEF_ERR_INVALID_ARG;
	if(n == 0)
		return DEF_SUCCESS;

	new_length = s->length + n;
	if(new_length > s->capacity)
	{
		result = Util_str_resize(s, new_length);
		if(result != DEF_SUCCESS)
			return result;
	}

	memcpy((s->buffer + s->length), p, n);
	s->buffer[new_length] = 0;
	s->length = new_length;
	s->sequential_id++;
	return DEF_SUCCESS;
}

static void Topos_md_bundle_clear(Topos_md_bundle* o)
{
	if(!o)
		return;
	free(o->sem_text);
	free(o->vid_text);
	o->sem_text = NULL;
	o->vid_text = NULL;
	o->sem_len = 0;
	o->vid_len = 0;
	o->fake_model = 255;
}

void Topos_md_bundle_free(Topos_md_bundle* out)
{
	Topos_md_bundle_clear(out);
}

static void parse_fake_line(const char* line, uint32_t len, uint8_t* out)
{
	while(len > 0 && (line[0] == ' ' || line[0] == '\t'))
	{
		line++;
		len--;
	}
	while(len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t' || line[len - 1] == '\r' || line[len - 1] == '\n'))
		len--;
	if(len == 0)
	{
		*out = 255;
		return;
	}
	if(len == 4 && strncmp(line, "auto", 4) == 0)
	{
		*out = 255;
		return;
	}
	if(len == 1 && line[0] == '0')
	{
		*out = 0;
		return;
	}
	if(len == 1 && line[0] == '1')
	{
		*out = 1;
		return;
	}
	*out = 255;
}

static char* dup_n(const char* s, uint32_t len)
{
	char* p = malloc((size_t)len + 1);
	if(!p)
		return NULL;
	if(len)
		memcpy(p, s, len);
	p[len] = 0;
	return p;
}

uint32_t Topos_md_read_bundle(Topos_md_bundle* out)
{
	uint8_t* raw = NULL;
	uint32_t rs = 0;
	uint32_t result = DEF_SUCCESS;

	Topos_md_bundle_clear(out);
	out->fake_model = 255;

	result = Util_file_load_from_file(DEF_TOPOS_SETTING_FILE, DEF_TOPOS_SETTING_DIR, &raw, TOPOS_MD_MAX_BYTES, 0, &rs);
	if(result != DEF_SUCCESS || !raw || rs == 0)
	{
		free(raw);
		(void)Topos_md_write_bundle("", 0, "", 0, 255);
		Topos_md_bundle_clear(out);
		out->fake_model = 255;
		return DEF_SUCCESS;
	}

	char* buf = malloc((size_t)rs + 1);
	if(!buf)
	{
		free(raw);
		return DEF_ERR_OUT_OF_MEMORY;
	}
	memcpy(buf, raw, rs);
	buf[rs] = 0;
	free(raw);

	static const char tag_fake[] = "## fake_model\n";
	static const char tag_sem[] = "## sem_settings\n";
	static const char tag_vid[] = "## vid_settings\n";

	char* pf = strstr(buf, tag_fake);
	char* ps = strstr(buf, tag_sem);
	char* pv = strstr(buf, tag_vid);

	if(pf)
	{
		char* line_start = pf + sizeof(tag_fake) - 1;
		char* line_end = strchr(line_start, '\n');
		uint32_t line_len = line_end ? (uint32_t)(line_end - line_start) : (uint32_t)strlen(line_start);
		parse_fake_line(line_start, line_len, &out->fake_model);
	}

	if(ps && pv)
	{
		char* sem_start = ps + sizeof(tag_sem) - 1;
		uint32_t sem_l = (uint32_t)(pv - sem_start);
		while(sem_l > 0 && (sem_start[sem_l - 1] == '\n' || sem_start[sem_l - 1] == '\r' || sem_start[sem_l - 1] == ' ' || sem_start[sem_l - 1] == '\t'))
			sem_l--;
		out->sem_text = dup_n(sem_start, sem_l);
		if(sem_l && !out->sem_text)
		{
			free(buf);
			return DEF_ERR_OUT_OF_MEMORY;
		}
		out->sem_len = sem_l;
	}
	else if(ps)
	{
		char* sem_start = ps + sizeof(tag_sem) - 1;
		uint32_t sem_l = (uint32_t)strlen(sem_start);
		while(sem_l > 0 && (sem_start[sem_l - 1] == '\n' || sem_start[sem_l - 1] == '\r'))
			sem_l--;
		out->sem_text = dup_n(sem_start, sem_l);
		if(sem_l && !out->sem_text)
		{
			free(buf);
			return DEF_ERR_OUT_OF_MEMORY;
		}
		out->sem_len = sem_l;
	}

	if(pv)
	{
		char* vid_start = pv + sizeof(tag_vid) - 1;
		uint32_t vid_l = (uint32_t)strlen(vid_start);
		while(vid_l > 0 && (vid_start[vid_l - 1] == '\n' || vid_start[vid_l - 1] == '\r' || vid_start[vid_l - 1] == ' ' || vid_start[vid_l - 1] == '\t'))
			vid_l--;
		out->vid_text = dup_n(vid_start, vid_l);
		if(vid_l && !out->vid_text)
		{
			free(buf);
			Topos_md_bundle_clear(out);
			return DEF_ERR_OUT_OF_MEMORY;
		}
		out->vid_len = vid_l;
	}

	free(buf);
	return DEF_SUCCESS;
}

uint32_t Topos_md_write_bundle(const char* sem, uint32_t sem_len, const char* vid, uint32_t vid_len, uint8_t fake_model)
{
	uint32_t result = DEF_ERR_OTHER;
	Str_data s = { 0, };

	Topos_md_boot_discard_stash();

	if(!sem)
		sem = "";
	if(!vid)
		vid = "";

	result = Util_str_init(&s);
	if(result != DEF_SUCCESS)
		return result;

	result = Util_str_add(&s, "# Topos settings (v1)\n\n## fake_model\n");
	if(result != DEF_SUCCESS)
		goto done;
	if(fake_model < 2)
		result = Util_str_add(&s, fake_model == 0 ? "0" : "1");
	else
		result = Util_str_add(&s, "auto");
	if(result != DEF_SUCCESS)
		goto done;

	result = Util_str_add(&s, "\n\n## sem_settings\n");
	if(result != DEF_SUCCESS)
		goto done;
	result = topos_str_append_raw(&s, sem, sem_len);
	if(result != DEF_SUCCESS)
		goto done;

	result = Util_str_add(&s, "\n\n## vid_settings\n");
	if(result != DEF_SUCCESS)
		goto done;
	result = topos_str_append_raw(&s, vid, vid_len);
	if(result != DEF_SUCCESS)
		goto done;

	result = Util_file_save_to_file(DEF_TOPOS_SETTING_FILE, DEF_TOPOS_SETTING_DIR, (uint8_t*)s.buffer, s.length, true);

done:
	Util_str_free(&s);
	return result;
}
