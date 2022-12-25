/*
Copyright (c) 2022 BostonBSD. All rights reserved.

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
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ctype.h>
#include <stdbool.h>

#include <glib-object.h>
#include <gtk/gtk.h>

#include "../include/class_types.h" /* portfolio_packet, window_data */

#include "../include/gui.h"
#include "../include/gui_globals.h" /* GtkBuilder *builder */
#include "../include/gui_types.h"   /* enums, etc */

#include "../include/csv.h"
#include "../include/macros.h"
#include "../include/multicurl.h"
#include "../include/mutex.h"
#include "../include/workfuncs.h"

static GtkListStore *rsi_completion_set_store(symbol_name_map *sn_map) {
  GtkListStore *store =
      gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
  GtkTreeIter iter;

  gchar item[35];
  /* Populate the GtkListStore with the string of stock symbols in column 0,
     stock names in column 1, and symbols & names in column 2. */
  for (gushort i = 0; i < sn_map->size; i++) {
    snprintf(item, 35, "%s - %s", sn_map->sn_container_arr[i]->symbol,
             sn_map->sn_container_arr[i]->security_name);

    gtk_list_store_append(store, &iter);
    /* Completion is going to match off of columns 0 and 1, but display column 2
     */
    /* Completion matches based off of the symbol or the company name, inserts
     * the symbol, displays both */
    gtk_list_store_set(store, &iter, 0, sn_map->sn_container_arr[i]->symbol, 1,
                       sn_map->sn_container_arr[i]->security_name, 2, item, -1);
  }
  return store;
}

static gboolean rsi_completion_match(GtkEntryCompletion *completion,
                                     const gchar *key, GtkTreeIter *iter,
                                     void *data) {
  /* We aren't using the data parameter, but the next statement
     silences a warning/error. */
  if (data != NULL)
    data = NULL;

  GtkTreeModel *model = gtk_entry_completion_get_model(completion);
  gchar *item_symb, *item_name;
  /* We are finding matches based off of column 0 and 1, however,
     we display column 2 in our 3 column model */
  gtk_tree_model_get(model, iter, 0, &item_symb, 1, &item_name, -1);
  gboolean ans = false, symbol_match = true, name_match = true;

  gushort N = 0;
  while (key[N]) {
    /* Only compare new key char if prev char was a match. */
    if (symbol_match)
      symbol_match = (tolower(key[N]) == tolower(item_symb[N]));
    if (name_match)
      name_match = (tolower(key[N]) == tolower(item_name[N]));
    /* Break the loop if both the symbol and the name are not a match. */
    if ((symbol_match == false) && (name_match == false))
      break;
    N++;
  }

  /* if either the symbol or the name match the key value, return true. */
  ans = symbol_match || name_match;
  g_free(item_symb);
  g_free(item_name);

  return ans;
}

int RSICompletionSet(void *data) {
  pthread_mutex_lock(&mutex_working[SYMBOL_NAME_MAP_MUTEX]);
  if (data == NULL) {
    pthread_mutex_unlock(&mutex_working[SYMBOL_NAME_MAP_MUTEX]);
    return 0;
  }

  GtkWidget *EntryBox =
      GTK_WIDGET(gtk_builder_get_object(builder, "ViewRSISymbolEntryBox"));
  GtkEntryCompletion *completion = gtk_entry_completion_new();
  GtkListStore *store = rsi_completion_set_store((symbol_name_map *)data);

  gtk_entry_completion_set_model(completion, GTK_TREE_MODEL(store));
  g_object_unref(G_OBJECT(store));
  gtk_entry_completion_set_match_func(
      completion, (GtkEntryCompletionMatchFunc)rsi_completion_match, NULL,
      NULL);
  /* Set RSIView entrybox completion widget. */
  gtk_entry_set_completion(GTK_ENTRY(EntryBox), completion);

  /* The text column to display is column 2 */
  gtk_entry_completion_set_text_column(completion, 2);
  gtk_entry_completion_set_inline_completion(completion, FALSE);
  gtk_entry_completion_set_inline_selection(completion, TRUE);
  gtk_entry_completion_set_popup_completion(completion, TRUE);
  /* Must type at least two characters for completion to make suggestions,
     reduces the number of results for single character keys. */
  gtk_entry_completion_set_minimum_key_length(completion, 2);
  /* The text column to insert is column 0
     We use a callback on the match-selected signal and insert the text from
     column 0 instead of column 2 We use a callback on the cursor-on-match
     signal and insert the text from column 0 instead of column 2
  */
  g_signal_connect(G_OBJECT(completion), "match-selected",
                   G_CALLBACK(GUICallbackHandler_select_comp), NULL);
  g_signal_connect(G_OBJECT(completion), "cursor-on-match",
                   G_CALLBACK(GUICallbackHandler_cursor_comp), NULL);

  g_object_unref(G_OBJECT(completion));

  pthread_mutex_unlock(&mutex_working[SYMBOL_NAME_MAP_MUTEX]);
  return 0;
}

int RSIShowHide(void *data) {
  portfolio_packet *pkg = (portfolio_packet *)data;
  window_data *W = pkg->GetWindowData();

  /* get the GObject and cast as a GtkWidget */
  GtkWidget *window =
      GTK_WIDGET(gtk_builder_get_object(builder, "ViewRSIWindow"));
  gboolean visible = gtk_widget_is_visible(window);

  if (visible) {
    gtk_widget_set_visible(window, false);
    gtk_window_resize(GTK_WINDOW(window), W->rsi_width, W->rsi_height);
    gtk_window_move(GTK_WINDOW(window), W->rsi_x_pos, W->rsi_y_pos);
  } else {
    gtk_window_resize(GTK_WINDOW(window), W->rsi_width, W->rsi_height);
    gtk_window_move(GTK_WINDOW(window), W->rsi_x_pos, W->rsi_y_pos);

    GtkWidget *Button =
        GTK_WIDGET(gtk_builder_get_object(builder, "ViewRSIFetchDataBTN"));
    gtk_widget_set_sensitive(Button, false);
    g_object_set(G_OBJECT(Button), "can-default", TRUE, "has-default", TRUE,
                 NULL);

    /* Reset EntryBox */
    GtkWidget *EntryBox =
        GTK_WIDGET(gtk_builder_get_object(builder, "ViewRSISymbolEntryBox"));
    gtk_entry_set_text(GTK_ENTRY(EntryBox), "");
    g_object_set(G_OBJECT(EntryBox), "activates-default", TRUE, NULL);
    gtk_widget_grab_focus(EntryBox);

    GtkWidget *Label =
        GTK_WIDGET(gtk_builder_get_object(builder, "ViewRSIStockSymbolLabel"));
    gtk_label_set_text(GTK_LABEL(Label), "");

    GtkWidget *scrwindow =
        GTK_WIDGET(gtk_builder_get_object(builder, "ViewRSIScrolledWindow"));
    gtk_scrolled_window_set_vadjustment(GTK_SCROLLED_WINDOW(scrwindow), NULL);

    gtk_widget_set_visible(window, true);
  }
  return 0;
}

int RSICursorMove() {
  GtkWidget *EntryBox =
      GTK_WIDGET(gtk_builder_get_object(builder, "ViewRSISymbolEntryBox"));
  GtkWidget *Button =
      GTK_WIDGET(gtk_builder_get_object(builder, "ViewRSIFetchDataBTN"));
  const gchar *s = gtk_entry_get_text(GTK_ENTRY(EntryBox));

  if (CheckValidString(s)) {
    gtk_widget_set_sensitive(Button, true);
  } else {
    gtk_widget_set_sensitive(Button, false);
  }

  return 0;
}

int RSITreeViewClear() {
  /* Clear the GtkTreeView. */
  GtkWidget *treeview =
      GTK_WIDGET(gtk_builder_get_object(builder, "ViewRSITreeView"));
  GtkTreeViewColumn *column;
  gushort n = gtk_tree_view_get_n_columns(GTK_TREE_VIEW(treeview));

  while (n) {
    n--;
    column = gtk_tree_view_get_column(GTK_TREE_VIEW(treeview), n);
    gtk_tree_view_remove_column(GTK_TREE_VIEW(treeview), column);
  }

  return 0;
}

static void rsi_set_columns() {
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  GtkWidget *list =
      GTK_WIDGET(gtk_builder_get_object(builder, "ViewRSITreeView"));

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes(
      "Date", renderer, "text", RSI_COLUMN_ONE, "foreground",
      RSI_FOREGROUND_COLOR, NULL);
  gtk_tree_view_column_set_resizable(column, true);
  gtk_tree_view_append_column(GTK_TREE_VIEW(list), column);

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes(
      "Price", renderer, "text", RSI_COLUMN_TWO, "foreground",
      RSI_FOREGROUND_COLOR, NULL);
  gtk_tree_view_column_set_resizable(column, true);
  gtk_tree_view_append_column(GTK_TREE_VIEW(list), column);

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes(
      "High", renderer, "text", RSI_COLUMN_THREE, "foreground",
      RSI_FOREGROUND_COLOR, NULL);
  gtk_tree_view_column_set_resizable(column, true);
  gtk_tree_view_append_column(GTK_TREE_VIEW(list), column);

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes(
      "Low", renderer, "text", RSI_COLUMN_FOUR, "foreground",
      RSI_FOREGROUND_COLOR, NULL);
  gtk_tree_view_column_set_resizable(column, true);
  gtk_tree_view_append_column(GTK_TREE_VIEW(list), column);

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes(
      "Opening", renderer, "text", RSI_COLUMN_FIVE, "foreground",
      RSI_FOREGROUND_COLOR, NULL);
  gtk_tree_view_column_set_resizable(column, true);
  gtk_tree_view_append_column(GTK_TREE_VIEW(list), column);

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes(
      "Prev Closing", renderer, "text", RSI_COLUMN_SIX, "foreground",
      RSI_FOREGROUND_COLOR, NULL);
  gtk_tree_view_column_set_resizable(column, true);
  gtk_tree_view_append_column(GTK_TREE_VIEW(list), column);

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes(
      "Chg ($)", renderer, "text", RSI_COLUMN_SEVEN, "foreground",
      RSI_FOREGROUND_COLOR, NULL);
  gtk_tree_view_column_set_resizable(column, true);
  gtk_tree_view_append_column(GTK_TREE_VIEW(list), column);

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes(
      "Gain (%)", renderer, "text", RSI_COLUMN_EIGHT, "foreground",
      RSI_FOREGROUND_COLOR, NULL);
  gtk_tree_view_column_set_resizable(column, true);
  gtk_tree_view_append_column(GTK_TREE_VIEW(list), column);

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes(
      "Volume", renderer, "text", RSI_COLUMN_NINE, "foreground",
      RSI_FOREGROUND_COLOR, NULL);
  gtk_tree_view_column_set_resizable(column, true);
  gtk_tree_view_append_column(GTK_TREE_VIEW(list), column);

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes(
      "RSI", renderer, "text", RSI_COLUMN_TEN, "foreground",
      RSI_FOREGROUND_COLOR, NULL);
  gtk_tree_view_column_set_resizable(column, true);
  gtk_tree_view_append_column(GTK_TREE_VIEW(list), column);

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes(
      "Indicator", renderer, "text", RSI_COLUMN_ELEVEN, "foreground",
      RSI_FOREGROUND_COLOR, NULL);
  gtk_tree_view_column_set_resizable(column, true);
  gtk_tree_view_append_column(GTK_TREE_VIEW(list), column);
}

int RSISetSNLabel(void *data) {
  gchar *sec_name;
  data ? (sec_name = (gchar *)data) : (sec_name = NULL);

  GtkWidget *Label =
      GTK_WIDGET(gtk_builder_get_object(builder, "ViewRSIStockSymbolLabel"));

  gtk_label_set_text(GTK_LABEL(Label), sec_name ? sec_name : "");
  if (sec_name)
    g_free(sec_name);

  return 0;
}

int RSIGetSymbol(char **s)
/* Get the stock symbol from the EntryBox.
   Must free return value.

   Because we aren't setting any widgets, we shouldn't worry about crashing
   Gtk outside the Gtk Main Loop.
*/
{
  GtkWidget *EntryBox =
      GTK_WIDGET(gtk_builder_get_object(builder, "ViewRSISymbolEntryBox"));
  s[0] = strdup(gtk_entry_get_text(GTK_ENTRY(EntryBox)));
  UpperCaseStr(s[0]);

  return 0;
}

enum { RUN, RESET };
bool rsi_ready(double gain_f, double *rsi_f, int state) {
  /* We need to remember the running averages
     and the counter between iterations. */
  static unsigned short c = 0;
  static double avg_gain_f = 0, avg_loss_f = 0;
  if (state == RESET) {
    c = 0;
    avg_gain_f = 0.0f;
    avg_loss_f = 0.0f;
    return false;
  }

  /* Until we get 14 days of data we sum the gains and losses. */
  if (c <= 13)
    Summation(gain_f, &avg_gain_f, &avg_loss_f);
  /* On the 14th day we calculate the regular average and use that to seed a
   * running average. */
  if (c == 13) {
    avg_gain_f = avg_gain_f / 14;
    avg_loss_f = avg_loss_f / 14;
  }
  /* On the 15th day we start calculating the RSI. */
  if (c >= 14) {
    /* Calculate the running average. */
    CalcAvg(gain_f, &avg_gain_f, &avg_loss_f);
    /* Calculate the rsi. */
    *rsi_f = CalcRsi(avg_gain_f, avg_loss_f);
    c++;
    return true;
  }
  c++;
  return false;
}

typedef struct {
  char *date_ch;
  char *price_ch;
  char *high_ch;
  char *low_ch;
  char *opening_ch;
  char *prev_closing_ch;
  char *change_ch;
  char *gain_ch;
  char *rsi_ch;
  char *volume_ch;
  const char *indicator_ch; /* Immutable, do not free */
  const char *fg_colr_ch;   /* Immutable, do not free */
} rsi_strings;

static bool rsi_calculate(char *line, rsi_strings *strings, int state) {
  /* We need to remember the last closing price in the next iteration. */
  static double cur_price_f = 0.0f;

  if (state == RESET) {
    /* Reset the static variables. */
    cur_price_f = 0.0f;
    rsi_ready(0.0, NULL, RESET);
    return false;
  }

  double gain_f, prev_price_f, rsi_f, change_f;
  unsigned long volume_long;
  const char *fg_colr_red_ch = "DarkRed";
  const char *fg_colr_green_ch = "DarkGreen";
  const char *fg_colr_black_ch = "Black";

  Chomp(line);
  char **csv_array = parse_csv(line);
  prev_price_f = cur_price_f;
  cur_price_f = strtod(csv_array[4], NULL);

  /* The initial closing price has no prev_price */
  if (prev_price_f == 0.0f) {
    free_csv_line(csv_array);
    return false;
  }

  gain_f = CalcGain(cur_price_f, prev_price_f);
  if (!rsi_ready(gain_f, &rsi_f, RUN)) {
    /* Until we get 14 days of data return false. */
    free_csv_line(csv_array);
    return false;
  }

  /* The RsiIndicator return value is stored in the stack, do not free. */
  strings->indicator_ch = RsiIndicator(rsi_f);
  CopyString(&strings->date_ch, csv_array[0]);
  DoubleToMonStr(&strings->prev_closing_ch, prev_price_f, 3);
  DoubleToMonStr(&strings->price_ch, cur_price_f, 3);
  StringToMonStr(&strings->high_ch, csv_array[2], 3);
  StringToMonStr(&strings->low_ch, csv_array[3], 3);
  StringToMonStr(&strings->opening_ch, csv_array[1], 3);
  change_f = cur_price_f - prev_price_f;
  DoubleToMonStr(&strings->change_ch, change_f, 3);
  DoubToPerStr(&strings->gain_ch, gain_f, 3);
  DoubToNumStr(&strings->rsi_ch, rsi_f, 3);
  volume_long = (unsigned long)strtol(csv_array[6], NULL, 10);
  DoubToNumStr(&strings->volume_ch, (double)volume_long, 0);
  /* fg_colr_ch is stored in the stack, do not free. */
  if (gain_f > 0) {
    strings->fg_colr_ch = fg_colr_green_ch;
  } else if (gain_f < 0) {
    strings->fg_colr_ch = fg_colr_red_ch;
  } else {
    strings->fg_colr_ch = fg_colr_black_ch;
  }
  free_csv_line(csv_array);
  return true;
}

static void rsi_set_store(GtkListStore *store, void *data) {
  MemType *MyOutputStruct;
  data ? (MyOutputStruct = (MemType *)data) : (MyOutputStruct = NULL);

  rsi_strings rsi_strs = (rsi_strings){NULL};

  GtkTreeIter iter;

  if (MyOutputStruct == NULL) {
    return;
  }
  if (MyOutputStruct->memory == NULL) {
    free(MyOutputStruct);
    return;
  }

  /* Convert a String to a File Pointer Stream for Reading */
  FILE *fp = fmemopen((void *)MyOutputStruct->memory,
                      strlen(MyOutputStruct->memory) + 1, "r");

  char line[1024];
  while (fgets(line, 1024, fp) != NULL) {
    /* If there is a null error in this line, ignore the line.
       Sometimes Yahoo! data is incomplete, the result is more
       correct if we ignore the incomplete portion of data. */
    /* Ignore the header line, which contains the 'Date' string */
    if (strstr(line, "null") || strstr(line, "Date"))
      continue;

    /* Don't start adding rows until we get 14 days of data. */
    if (!rsi_calculate(line, &rsi_strs, RUN))
      continue;

    /* Add data to the storage container. */
    /* Yahoo! sends data with the earliest date first, so we prepend rows.
       The last [most recent] entry needs to be at the top. */
    gtk_list_store_prepend(store, &iter);
    gtk_list_store_set(
        store, &iter, RSI_FOREGROUND_COLOR, rsi_strs.fg_colr_ch, RSI_COLUMN_ONE,
        rsi_strs.date_ch, RSI_COLUMN_TWO, rsi_strs.price_ch, RSI_COLUMN_THREE,
        rsi_strs.high_ch, RSI_COLUMN_FOUR, rsi_strs.low_ch, RSI_COLUMN_FIVE,
        rsi_strs.opening_ch, RSI_COLUMN_SIX, rsi_strs.prev_closing_ch,
        RSI_COLUMN_SEVEN, rsi_strs.change_ch, RSI_COLUMN_EIGHT,
        rsi_strs.gain_ch, RSI_COLUMN_NINE, rsi_strs.volume_ch, RSI_COLUMN_TEN,
        rsi_strs.rsi_ch, RSI_COLUMN_ELEVEN, rsi_strs.indicator_ch, -1);
  }
  /* Reset the static variables. */
  rsi_calculate(NULL, NULL, RESET);
  fclose(fp);

  /* Free MyOutputStruct */
  free(MyOutputStruct->memory);
  free(MyOutputStruct);

  free(rsi_strs.date_ch);
  free(rsi_strs.gain_ch);
  free(rsi_strs.rsi_ch);
  free(rsi_strs.volume_ch);
  free(rsi_strs.price_ch);
  free(rsi_strs.high_ch);
  free(rsi_strs.low_ch);
  free(rsi_strs.opening_ch);
  free(rsi_strs.change_ch);
  free(rsi_strs.prev_closing_ch);
}

static GtkListStore *rsi_make_store(void *data) {
  GtkListStore *store;

  /* Set up the storage container with the number of columns and column type */
  store = gtk_list_store_new(
      RSI_N_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

  /* Add data to the storage container, pass in the outputstruct pointer. */
  /* This function frees data. */
  rsi_set_store(store, data);
  return store;
}

int RSIMakeTreeview(void *data) {
  GtkListStore *store = NULL;
  GtkWidget *list =
      GTK_WIDGET(gtk_builder_get_object(builder, "ViewRSITreeView"));

  /* Set the columns for the new TreeView model */
  rsi_set_columns();

  /* Set up the storage container, pass in the outputstruct pointer. */
  /* This function frees data. */
  store = rsi_make_store(data);

  /* Add the store of data to the list. */
  gtk_tree_view_set_model(GTK_TREE_VIEW(list), GTK_TREE_MODEL(store));
  g_object_unref(store);

  /* Set the list header as visible. */
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(list), TRUE);

  /* Remove Grid Lines. */
  gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(list),
                               GTK_TREE_VIEW_GRID_LINES_NONE);

  return 0;
}