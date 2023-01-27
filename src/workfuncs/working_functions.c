/*
Copyright (c) 2022-2023 BostonBSD. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    (1) Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

    (2) Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in
    the documentation and/or other materials provided with the
    distribution.

    (3)The name of the author may not be used to
    endorse or promote products derived from this software without
    specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*/

#include "../include/class_types.h"
#include "../include/macros.h"

gdouble CalcGain(gdouble cur_price, gdouble prev_price) {
  return (100 * ((cur_price - prev_price) / prev_price));
}

void CalcSumRsi(gdouble current_gain, gdouble *avg_gain, gdouble *avg_loss) {
  if (current_gain >= 0) {
    *avg_gain += current_gain;
  } else {
    *avg_loss += (-1 * current_gain);
  }
}

void CalcRunAvgRsi(gdouble current_gain, gdouble *avg_gain, gdouble *avg_loss) {
  if (current_gain >= 0) {
    *avg_gain = ((*avg_gain * 13) + current_gain) / 14;
    *avg_loss = ((*avg_loss * 13) + 0) / 14;
  } else {
    *avg_gain = ((*avg_gain * 13) + 0) / 14;
    *avg_loss = ((*avg_loss * 13) + (-1 * current_gain)) / 14;
  }
}

gdouble CalcRsi(gdouble avg_gain, gdouble avg_loss) {
  gdouble rs = avg_gain / avg_loss;
  return (100 - (100 / (1 + rs)));
}

gchar *ExtractYahooData(FILE *fp, gdouble *prev_closing_f, gdouble *cur_price_f)
/* Take in a file pointer, and references to two doubles; prev_closing_f and
   cur_price_f.  Will populate the last closing price and the current price.

   Returns the last line of the file stream.
   Must free return value.

   Useful for finding the current stats on a security/index/commodity from
   Yahoo! finance.
*/
{
  gchar line[1024];
  gchar **csv_array;

  /* Yahoo! sometimes updates data when the equities markets are closed.
     The while loop iterates to the end of file to get the latest data. */
  *prev_closing_f = 0.0f;
  *cur_price_f = 0.0f;
  while (fgets(line, 1024, fp) != NULL) {
    g_strchomp(line);

    *prev_closing_f = *cur_price_f;
    /* Sometimes the API gives us a null value for certain days.
       using the closing price from the day prior gives us a more accurate
       gain value. */
    /* If we have an empty line, continue. */
    if (g_strrstr(line, "null") || g_strrstr(line, "Date") || line[0] == '\0')
      continue;

    /* Invalid replies start with a tag. */
    if (g_strrstr(line, "<")) {
      return NULL;
    }

    csv_array = g_strsplit(line, ",", -1);
    if (g_strv_length(csv_array) < 7) {
      g_strfreev(csv_array);
      return NULL;
    }
    *cur_price_f = g_strtod(csv_array[4] ? csv_array[4] : "0", NULL);
    g_strfreev(csv_array);
  };
  return (gchar *)g_strdup(line);
}

static gint64 unix_time_sec() {
  /* Return the unix time in seconds; rounded down to the nearest second */
  gint64 time_usec = g_get_real_time();
  return (time_usec - (time_usec % G_TIME_SPAN_SECOND)) / G_TIME_SPAN_SECOND;
}

void GetYahooUrl(gchar **url_ch, const gchar *symbol_ch, guint period) {
  gint64 end_time, start_time;
  gushort len;

  end_time = unix_time_sec();
  start_time = end_time - (gint64)period;

  const gchar *fmt = YAHOO_URL_START
      "%s" YAHOO_URL_MIDDLE_ONE "%ld" YAHOO_URL_MIDDLE_TWO "%ld" YAHOO_URL_END;

  len = g_snprintf(NULL, 0, fmt, symbol_ch, start_time, end_time) + 1;
  gchar *tmp = g_realloc(url_ch[0], len);

  if (tmp == NULL) {
    printf("Not Enough Memory, realloc returned NULL.\n");
    exit(EXIT_FAILURE);
  }

  url_ch[0] = tmp;
  g_snprintf(url_ch[0], len, fmt, symbol_ch, start_time, end_time);
}