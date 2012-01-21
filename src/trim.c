/* libSoX effect: trim - cut portions out of the audio
 *
 * First version written 01/2012 by Ulrich Klauer.
 * Replaces an older trim effect originally written by Curt Zirzow in 2000.
 *
 * Copyright 2012 Chris Bagwell and SoX Contributors
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "sox_i.h"

typedef struct {
  /* parameters */
  unsigned int num_pos;
  struct {
    uint64_t sample; /* NB: wide samples */
    char *argstr;
    enum {
      a_start, a_latest, a_end
    } anchor;
  } *pos;
  /* state */
  unsigned int current_pos;
  uint64_t samples_read; /* NB: wide samples */
  sox_bool copying;
  sox_bool uses_end;
} priv_t;

static int parse(sox_effect_t *effp, int argc, char **argv)
{
  priv_t *p = (priv_t*) effp->priv;
  unsigned int i;
  --argc, ++argv;
  p->num_pos = argc;
  lsx_Calloc(p->pos, p->num_pos);
  p->uses_end = sox_false;
  for (i = 0; i < p->num_pos; i++) {
    uint64_t dummy;
    const char *arg = argv[i];
    if (arg[0] == '=') {
      p->pos[i].anchor = a_start;
      arg++;
    } else if (arg[0] == '-') {
      p->pos[i].anchor = a_end;
      p->uses_end = sox_true;
      arg++;
    } else
      p->pos[i].anchor = a_latest;
    p->pos[i].argstr = lsx_strdup(arg);
    /* dummy parse to check for syntax errors */
    arg = lsx_parsesamples(0., arg, &dummy, 't');
    if (!arg || *arg)
      return lsx_usage(effp);
  }
  return SOX_SUCCESS;
}

static int start(sox_effect_t *effp)
{
  priv_t *p = (priv_t*) effp->priv;
  uint64_t in_length = effp->in_signal.length != SOX_UNKNOWN_LEN ?
    effp->in_signal.length / effp->in_signal.channels : SOX_UNKNOWN_LEN;
  uint64_t last_seen = 0;
  sox_bool open_end;
  unsigned int i;

  p->copying = sox_false;

  /* calculate absolute positions */
  if (in_length == SOX_UNKNOWN_LEN && p->uses_end) {
    lsx_fail("Can't use positions relative to end: audio length is unknown.");
    return SOX_EOF;
  }
  for (i = 0; i < p->num_pos; i++) {
    uint64_t s, res = 0;
    if (!lsx_parsesamples(effp->in_signal.rate, p->pos[i].argstr, &s, 't'))
      return lsx_usage(effp);
    switch (p->pos[i].anchor) {
      case a_start: res = s; break;
      case a_latest: res = last_seen + s; break;
      case a_end:
        if (s > in_length) {
          lsx_fail("Position %u is before start of audio.", i+1);
          return SOX_EOF;
        }
        res = in_length - s;
        break;
    }
    last_seen = p->pos[i].sample = res;
    lsx_debug_more("position %u at %" PRIu64, i+1, res);
  }

  /* sanity checks */
  last_seen = 0;
  for (i = 0; i < p->num_pos; i++) {
    if (p->pos[i].sample < last_seen) {
      lsx_fail("Position %u is behind the following position.", i);
      return SOX_EOF;
    }
    last_seen = p->pos[i].sample;
  }
  if (p->num_pos && in_length != SOX_UNKNOWN_LEN &&
      p->pos[0].sample > in_length) {
    lsx_fail("Start position after end of audio.");
    return SOX_EOF;
  }
  if (p->num_pos && in_length != SOX_UNKNOWN_LEN &&
      p->pos[p->num_pos-1].sample > in_length) {
    lsx_fail("End position after end of audio.");
    return SOX_EOF;
  }

  if (p->num_pos == 1 && !p->pos[0].sample)
    return SOX_EFF_NULL;

  /* calculate output length */
  open_end = p->num_pos % 2;
  if (open_end && in_length == SOX_UNKNOWN_LEN)
    effp->out_signal.length = SOX_UNKNOWN_LEN;
  else {
    effp->out_signal.length = 0;
    for (i = 0; i+1 < p->num_pos ; i += 2)
      effp->out_signal.length +=
        p->pos[i+1].sample - p->pos[i].sample;
    if (open_end)
      effp->out_signal.length +=
        in_length - p->pos[p->num_pos-1].sample;
    effp->out_signal.length *= effp->in_signal.channels;
  }

  return SOX_SUCCESS;
}

static int flow(sox_effect_t *effp, const sox_sample_t *ibuf,
    sox_sample_t *obuf, size_t *isamp, size_t *osamp)
{
  priv_t *p = (priv_t*) effp->priv;
  size_t len = min(*isamp, *osamp);
  size_t channels = effp->in_signal.channels;
  len /= channels;
  *isamp = *osamp = 0;

  while (len) {
    size_t chunk;

    if (p->current_pos < p->num_pos &&
        p->samples_read == p->pos[p->current_pos].sample) {
      p->copying = !p->copying;
      p->current_pos++;
    }

    if (p->current_pos >= p->num_pos && !p->copying)
      return SOX_EOF;

    chunk = p->current_pos < p->num_pos ?
      min(len, p->pos[p->current_pos].sample - p->samples_read) : len;
    if (p->copying) {
      memcpy(obuf, ibuf, chunk * channels * sizeof(*obuf));
      obuf += chunk * channels, *osamp += chunk * channels;
    }
    ibuf += chunk * channels; *isamp += chunk * channels;
    p->samples_read += chunk, len -= chunk;
  }

  return SOX_SUCCESS;
}

static int drain(sox_effect_t *effp, sox_sample_t *obuf UNUSED, size_t *osamp)
{
  priv_t *p = (priv_t*) effp->priv;
  *osamp = 0;
  if (p->current_pos < p->num_pos)
    lsx_warn("Audio shorter than expected; last %u positions not reached.",
      p->num_pos - p->current_pos);
  return SOX_EOF;
}

static int lsx_kill(sox_effect_t *effp)
{
  unsigned int i;
  priv_t *p = (priv_t*) effp->priv;
  for (i = 0; i < p->num_pos; i++)
    free(p->pos[i].argstr);
  free(p->pos);
  return SOX_SUCCESS;
}

sox_effect_handler_t const *lsx_trim_effect_fn(void)
{
  static sox_effect_handler_t handler = {
    "trim", "{[=|-]position}",
    SOX_EFF_MCHAN | SOX_EFF_LENGTH | SOX_EFF_MODIFY,
    parse, start, flow, drain, NULL, lsx_kill,
    sizeof(priv_t)
  };
  return &handler;
}

/* The following functions allow a libSoX client to do a speed
 * optimization, by asking for the number of samples to be skipped
 * at the beginning of the audio with sox_trim_get_start(), skipping
 * that many samples in an efficient way such as seeking within the
 * input file, then telling us it has been done by calling
 * sox_trim_clear_start() (the name is historical).
 * Note that sox_trim_get_start() returns the number of non-wide
 * samples. */

sox_uint64_t sox_trim_get_start(sox_effect_t *effp)
{
    priv_t *p = (priv_t*) effp->priv;
    return p->num_pos ? p->pos[0].sample * effp->in_signal.channels : 0;
}

void sox_trim_clear_start(sox_effect_t *effp)
{
    priv_t *p = (priv_t*) effp->priv;
    p->samples_read = p->num_pos ? p->pos[0].sample : 0;
}
