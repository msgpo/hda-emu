/*
 * hda-emu - simple HD-audio codec emulator for debugging snd-hda-intel driver
 *
 * Misc wrappers
 *
 * Copyright (c) Takashi Iwai <tiwai@suse.de>
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <sound/driver.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <assert.h>
#include "kernel/hda_codec.h"
#include "hda-types.h"
#include "hda-log.h"

int snd_pcm_format_width(int format)
{
	switch (format) {
	case SNDRV_PCM_FORMAT_U8:
		return 8;
	case SNDRV_PCM_FORMAT_S16_LE:
		return 16;
	case SNDRV_PCM_FORMAT_S32_LE:
	case SNDRV_PCM_FORMAT_FLOAT_LE:
	case SNDRV_PCM_FORMAT_IEC958_SUBFRAME_LE:
		return 32;
	default:
		return -EINVAL;
	}
}

int snd_hda_create_hwdep(struct hda_codec *codec)
{
	return 0;
}

int snd_hda_hwdep_add_sysfs(struct hda_codec *codec)
{
	return 0;
}

/*
 */
void (*snd_iprintf_dumper)(struct snd_info_buffer *buf,
			   const char *fmt, va_list ap);

void snd_iprintf(struct snd_info_buffer *buf, const char *fmt, ...)
{
	va_list ap;
	if (!snd_iprintf_dumper)
		return;
	va_start(ap, fmt);
	snd_iprintf_dumper(buf, fmt, ap);
	va_end(ap);
}

/* there is a compat wrapper in the latest SLE11 kernel */
#ifndef snd_pci_quirk_lookup

/*
 * quirk lookup
 */
const struct snd_pci_quirk *
snd_pci_quirk_lookup(struct pci_dev *pci, const struct snd_pci_quirk *list)
{
	const struct snd_pci_quirk *q;

	for (q = list; q->subvendor; q++) {
		if (q->subvendor != pci->subsystem_vendor)
			continue;
#ifdef NEW_QUIRK_LIST
		if (!q->subdevice ||
		    (pci->subsystem_device & q->subdevice_mask) == q->subdevice)
			return q;
#else
		if (!q->subdevice ||
		    q->subdevice == pci->subsystem_device)
			return q;
#endif
	}
	return NULL;
}

#endif /* snd_pci_quirk_lookup */

/* malloc debug */
#ifdef DEBUG_MALLOC
struct __hda_malloc_elem {
	void *ptr;
	const char *file;
	int line;
	struct list_head list;
};

static LIST_HEAD(malloc_list);

void *__hda_malloc(size_t size, const char *file, int line)
{
	struct __hda_malloc_elem *elem = malloc(sizeof(*elem));
	if (!elem)
		return NULL;
	elem->ptr = calloc(1, size);
	if (!elem->ptr) {
		free(elem);
		return NULL;
	}
	elem->file = file;
	elem->line = line;
	list_add_tail(&elem->list, &malloc_list);
	return elem->ptr;
}

void __hda_free(void *ptr, const char *file, int line)
{
	struct __hda_malloc_elem *elem;

	if (!ptr)
		return;

	list_for_each_entry(elem, &malloc_list, list) {
		if (elem->ptr == ptr) {
			list_del(&elem->list);
			free(elem->ptr);
			free(elem);
			return;
		}
	}
	hda_log(HDA_LOG_ERR, "Untracked malloc freed in %s:%d\n",
		file, line);
	assert(0);
}

void *__hda_realloc(const void *p, size_t new_size, const char *file, int line)
{
	struct __hda_malloc_elem *elem;

	if (!p)
		return __hda_malloc(new_size, file, line);
	if (!new_size) {
		__hda_free((void *)p, file, line);
		return NULL;
	}

	list_for_each_entry(elem, &malloc_list, list) {
		if (elem->ptr == p) {
			void *nptr = realloc((void *)p, new_size);
			if (nptr)
				elem->ptr = nptr;
			return nptr;
		}
	}
	hda_log(HDA_LOG_ERR, "Untracked malloc realloced in %s:%d\n",
		file, line);
	return __hda_malloc(new_size, file, line);
}

void *__hda_strdup(const char *str, const char *file, int line)
{
	char *dest = __hda_malloc(strlen(str) + 1, file, line);
	if (!dest)
		return NULL;
	strcpy(dest, str);
	return dest;
}
#endif /* DEBUG_MALLOC */

/* jack API */
#include <sound/jack.h>
int snd_jack_new(struct snd_card *card, const char *id, int type,
		 struct snd_jack **jack)
{
	struct snd_jack *jp;

	jp = calloc(1, sizeof(*jp));
	if (!jp)
		return -ENOMEM;
	jp->id = strdup(id);
	if (!jp->id)
		return -ENOMEM;
	jp->type = type;
	hda_log(HDA_LOG_INFO, "JACK created %s, type %d\n", id, type);
	*jack = jp;
	return 0;
}

void snd_jack_set_parent(struct snd_jack *jack, struct device *parent)
{
	/* NOP */
}

void snd_jack_report(struct snd_jack *jack, int status)
{
	hda_log(HDA_LOG_INFO, "JACK report %s, status %d\n", jack->id, status);
}

/*
 * lock
 */
void mylock_init(int *lock)
{
	*lock = MYLOCK_UNLOCKED;
}

void mylock_lock(int *lock, const char *file, int line)
{
	switch (*lock) {
	case MYLOCK_UNINIT:
		hda_log(HDA_LOG_ERR, "Locking uninitialized at %s:%d\n",
			file, line);
		break;
	case MYLOCK_UNLOCKED:
		*lock = MYLOCK_LOCKED;
		break;
	case MYLOCK_LOCKED:
		hda_log(HDA_LOG_ERR, "Double-lock detected at %s:%d\n",
			file, line);
		break;
	default:
		hda_log(HDA_LOG_ERR, "Unknown lock state %d! at %s:%d\n",
			*lock, file, line);
		break;
	}
}

void mylock_unlock(int *lock, const char *file, int line)
{
	switch (*lock) {
	case MYLOCK_UNINIT:
		hda_log(HDA_LOG_ERR, "Unlocking uninitialized at %s:%d\n",
			file, line);
		break;
	case MYLOCK_UNLOCKED:
		hda_log(HDA_LOG_ERR, "Double-unlock detected at %s:%d\n",
			file, line);
		break;
	case MYLOCK_LOCKED:
		*lock = MYLOCK_UNLOCKED;
		break;
	default:
		hda_log(HDA_LOG_ERR, "Unknown lock state %d! at %s:%d\n",
			*lock, file, line);
		break;
	}
}
