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

#include "../include/class.h" /* The class init and destruct funcs are required 
                                       in the class methods, includes portfolio_packet 
                                       metal, meta, and equity_folder class types */
#include "../include/multicurl.h"
#include "../include/mutex.h"
#include "../include/sqlite.h"
#include "../include/workfuncs.h"

/* The global variable 'packet', which is externed in globals.h, is always
 * accessed via these functions. */
/* This is an ad-hoc way of self referencing a class.
   It prevents multiple instances of the portfolio_packet class. */

portfolio_packet *packet;

/* Class Method (also called Function) Definitions */
static gint perform_multicurl_request(portfolio_packet *pkg) {
  equity_folder *F = pkg->GetEquityFolderClass();
  gint return_code = 0;
  gushort num_metals = 2;
  if (pkg->metal_class->Platinum->ounce_f > 0)
    num_metals++;
  if (pkg->metal_class->Palladium->ounce_f > 0)
    num_metals++;

  /* Perform the cURL requests simultaneously using multi-cURL. */
  /* Four Indices plus Two-to-Four Metals plus Number of Equities */
  return_code = PerformMultiCurl(pkg->multicurl_main_hnd,
                                 4.0f + (double)num_metals + (double)F->size);
  if (return_code)
    pkg->FreeMainCurlData();

  return return_code;
}

static gint GetData() {
  g_mutex_lock(&mutexes[CLASS_MEMBER_MUTEX]);
  gint return_code = 0;

  /* We don't want to remove handles while setting up curl. */
  g_mutex_lock(&mutexes[MULTICURL_REM_HAND_MUTEX]);

  packet->meta_class->SetUpCurlIndicesData(packet); /* Four Indices */
  packet->metal_class->SetUpCurl(packet);           /* Two to Four Metals */
  packet->equity_folder_class->SetUpCurl(packet);

  /* The user might want to remove handles during perform_multicurl_request().
   */
  g_mutex_unlock(&mutexes[MULTICURL_REM_HAND_MUTEX]);

  return_code = (gint)perform_multicurl_request(packet);

  g_mutex_unlock(&mutexes[CLASS_MEMBER_MUTEX]);
  return return_code;
}

static void ExtractData() {
  g_mutex_lock(&mutexes[CLASS_EXTRACT_DATA_MUTEX]);

  packet->meta_class->ExtractIndicesData();
  packet->metal_class->ExtractData();
  packet->equity_folder_class->ExtractData();

  g_mutex_unlock(&mutexes[CLASS_EXTRACT_DATA_MUTEX]);
}

static void Calculate() {
  g_mutex_lock(&mutexes[CLASS_CALCULATE_MUTEX]);

  packet->equity_folder_class->Calculate();
  packet->metal_class->Calculate();
  packet->meta_class->CalculatePortfolio(packet);
  /* No need to calculate the index data [the gain calculation is performed
   * during extraction] */

  g_mutex_unlock(&mutexes[CLASS_CALCULATE_MUTEX]);
}

static void ToStrings() {
  g_mutex_lock(&mutexes[CLASS_TOSTRINGS_MUTEX]);

  packet->meta_class->ToStringsPortfolio();
  packet->meta_class->ToStringsIndices();
  packet->metal_class->ToStrings(packet->meta_class->decimal_places_guint8);
  packet->equity_folder_class->ToStrings(
      packet->meta_class->decimal_places_guint8);

  g_mutex_unlock(&mutexes[CLASS_TOSTRINGS_MUTEX]);
}

static gdouble GetHoursOfUpdates() {
  return packet->meta_class->updates_hours_f;
}

static gdouble GetUpdatesPerMinute() {
  return packet->meta_class->updates_per_min_f;
}

static void FreeMainCurlData() {
  g_mutex_lock(&mutexes[CLASS_EXTRACT_DATA_MUTEX]);

  equity_folder *F = packet->GetEquityFolderClass();
  metal *M = packet->GetMetalClass();
  meta *Met = packet->GetMetaClass();

  for (guint8 c = 0; c < F->size; c++) {
    FreeMemtype(&F->Equity[c]->JSON);
  }
  FreeMemtype(&M->Gold->CURLDATA);
  FreeMemtype(&M->Silver->CURLDATA);
  FreeMemtype(&M->Platinum->CURLDATA);
  FreeMemtype(&M->Palladium->CURLDATA);

  FreeMemtype(&Met->INDEX_DOW_CURLDATA);
  FreeMemtype(&Met->INDEX_NASDAQ_CURLDATA);
  FreeMemtype(&Met->INDEX_SP_CURLDATA);
  FreeMemtype(&Met->CRYPTO_BITCOIN_CURLDATA);

  g_mutex_unlock(&mutexes[CLASS_EXTRACT_DATA_MUTEX]);
}

static void remove_main_curl_handles(portfolio_packet *pkg)
/* Removing the easy handle from the multihandle will stop the cURL data
   transfer immediately. curl_multi_remove_handle does nothing if the easy
   handle is not currently set in the multihandle.

   FYI: If the handles are removed during a transfer, curl appears to throw
   a unix signal which will prevent canceling a thread during sleep in pthreads
   or signalling a cond in Gthreads during g_cond_wait_until().  Use a flag in
   conjunction, to exit the thread before the sleep() or g_cond_wait()
   statement.
  */
{
  g_mutex_lock(&mutexes[MULTICURL_REM_HAND_MUTEX]);

  metal *M = pkg->GetMetalClass();
  equity_folder *F = pkg->GetEquityFolderClass();
  meta *Met = pkg->GetMetaClass();

  curl_multi_wakeup(pkg->multicurl_main_hnd);
  g_mutex_lock(&mutexes[MULTICURL_PROG_MUTEX]);

  /* Equity Multicurl Operation */
  for (guint8 i = 0; i < F->size; i++) {
    curl_multi_remove_handle(pkg->multicurl_main_hnd, F->Equity[i]->easy_hnd);
  }

  /* Bullion Multicurl Operation */
  curl_multi_remove_handle(pkg->multicurl_main_hnd, M->Gold->YAHOO_hnd);
  curl_multi_remove_handle(pkg->multicurl_main_hnd, M->Silver->YAHOO_hnd);
  if (M->Platinum->ounce_f > 0)
    curl_multi_remove_handle(pkg->multicurl_main_hnd, M->Platinum->YAHOO_hnd);
  if (M->Palladium->ounce_f > 0)
    curl_multi_remove_handle(pkg->multicurl_main_hnd, M->Palladium->YAHOO_hnd);

  /* Indices Multicurl Operation */
  curl_multi_remove_handle(pkg->multicurl_main_hnd, Met->index_dow_hnd);
  curl_multi_remove_handle(pkg->multicurl_main_hnd, Met->index_nasdaq_hnd);
  curl_multi_remove_handle(pkg->multicurl_main_hnd, Met->index_sp_hnd);
  curl_multi_remove_handle(pkg->multicurl_main_hnd, Met->crypto_bitcoin_hnd);

  g_mutex_unlock(&mutexes[MULTICURL_PROG_MUTEX]);

  pkg->FreeMainCurlData();

  g_mutex_unlock(&mutexes[MULTICURL_REM_HAND_MUTEX]);
}

static void StopMultiCurlMain() {
  /* Main Window Data Fetch Multicurl Operation */
  remove_main_curl_handles(packet);
}

static void StopMultiCurlAll() {
  meta *D = packet->GetMetaClass();

  /* Symbol Name Fetch Multicurl Operation */
  D->StopSNMapCurl();

  /* History Data Multicurl Operation */
  D->StopHistoryCurl();

  /* Main Window Data Fetch Multicurl Operation */
  remove_main_curl_handles(packet);
}

static gpointer GetPrimaryHeadings() { return &packet->meta_class->pri_h_mkd; }

static gpointer GetDefaultHeadings() { return &packet->meta_class->def_h_mkd; }

static gpointer GetWindowData() { return &packet->meta_class->window_struct; }

static gpointer GetMetaClass() { return packet->meta_class; }

static gpointer GetMetalClass() { return packet->metal_class; }

static gpointer GetEquityFolderClass() { return packet->equity_folder_class; }

static gpointer GetSymNameMap() { return packet->meta_class->sym_map; }

static void SetSymNameMap(gpointer data) {
  packet->meta_class->sym_map = (symbol_name_map *)data;
}

static gboolean IsClockDisplayed() {
  return packet->meta_class->clocks_displayed_bool;
}

static void SetClockDisplayed(gboolean data) {
  packet->meta_class->clocks_displayed_bool = data;
}

static gboolean IsIndicesDisplayed() {
  return packet->meta_class->index_bar_revealed_bool;
}

static void SetIndicesDisplayed(gboolean data) {
  packet->meta_class->index_bar_revealed_bool = data;
}

static gboolean IsClosed()
/* Market Closed flag */
{
  if (!packet->meta_class->clocks_displayed_bool) {
    packet->meta_class->market_closed_bool =
        GetTimeData(NULL, NULL, NULL, NULL, NULL, NULL, NULL);
  }
  return packet->meta_class->market_closed_bool;
}

static void SetClosed(gboolean data)
/* Market Closed flag */
{
  packet->meta_class->market_closed_bool = data;
}

static gboolean IsMainCurlCanceled()
/* Main window curl flag */
{
  return packet->meta_class->multicurl_cancel_main_bool;
}

static void SetMainCurlCanceled(gboolean data)
/* Main window curl flag */
{
  packet->meta_class->multicurl_cancel_main_bool = data;
}

static gboolean IsFetchingData() {
  return packet->meta_class->fetching_data_bool;
}

static void SetFetchingData(gboolean data) {
  packet->meta_class->fetching_data_bool = data;
}

static gboolean IsDefaultView() {
  return packet->meta_class->main_win_default_view_bool;
}

static void SetDefaultView(gboolean data) {
  packet->meta_class->main_win_default_view_bool = data;
}

static gboolean IsCurlCanceled()
/* Non-main curl flag */
{
  return packet->meta_class->multicurl_cancel_bool;
}

static void SetCurlCanceled(gboolean data)
/* Non-main curl flag */
{
  packet->meta_class->multicurl_cancel_bool = data;
}

static void SetSecurityNames() {
  packet->equity_folder_class->SetSecurityNames(packet);
}

static void save_sql_data_bul(const gchar *metal_name_ch,
                              const gdouble ounces_f, const gdouble premium_f) {
  meta *D = packet->GetMetaClass();

  gchar *ounces_ch, *premium_ch;
  ounces_ch = g_markup_printf_escaped("%lf", ounces_f);
  premium_ch = g_markup_printf_escaped("%lf", premium_f);
  SqliteBullionAdd(metal_name_ch, ounces_ch, premium_ch, D);
  g_free(ounces_ch);
  g_free(premium_ch);
}

static void SaveSqlData() {
  metal *M = packet->GetMetalClass();
  meta *D = packet->GetMetaClass();
  window_data *W = packet->GetWindowData();

  /* Save the Window Size and Location. */
  SqliteMainWindowSizeAdd(W->main_width, W->main_height, D);
  SqliteMainWindowPosAdd(W->main_x_pos, W->main_y_pos, D);
  SqliteHistoryWindowSizeAdd(W->history_width, W->history_height, D);
  SqliteHistoryWindowPosAdd(W->history_x_pos, W->history_y_pos, D);

  /* Save preference info. */
  SqlitePrefAdd("Main_Font", D->font_ch, D);
  if (D->clocks_displayed_bool) {
    SqlitePrefAdd("Clocks_Displayed", "TRUE", D);
  } else {
    SqlitePrefAdd("Clocks_Displayed", "FALSE", D);
  }
  if (D->index_bar_revealed_bool) {
    SqlitePrefAdd("Indices_Displayed", "TRUE", D);
  } else {
    SqlitePrefAdd("Indices_Displayed", "FALSE", D);
  }

  gchar *value = g_markup_printf_escaped("%d", D->decimal_places_guint8);
  SqlitePrefAdd("Decimal_Places", value, D);
  g_free(value);

  value = g_markup_printf_escaped("%lf", D->updates_per_min_f);
  SqlitePrefAdd("Updates_Per_Min", value, D);
  g_free(value);

  value = g_markup_printf_escaped("%lf", D->updates_hours_f);
  SqlitePrefAdd("Updates_Hours", value, D);
  g_free(value);

  /* Save api info. */
  SqliteAPIAdd("Stock_URL", D->stock_url_ch, D);
  SqliteAPIAdd("URL_KEY", D->curl_key_ch, D);
  SqliteAPIAdd("Nasdaq_Symbol_URL", D->Nasdaq_Symbol_url_ch, D);
  SqliteAPIAdd("NYSE_Symbol_URL", D->NYSE_Symbol_url_ch, D);

  /* Save cash info. */
  value = g_markup_printf_escaped("%lf", D->cash_f);
  SqliteCashAdd(value, D);
  g_free(value);

  /* Save bullion info. */
  save_sql_data_bul("gold", M->Gold->ounce_f, M->Gold->premium_f);
  save_sql_data_bul("silver", M->Silver->ounce_f, M->Silver->premium_f);
  save_sql_data_bul("platinum", M->Platinum->ounce_f, M->Platinum->premium_f);
  save_sql_data_bul("palladium", M->Palladium->ounce_f,
                    M->Palladium->premium_f);

  /* It's easier to save / remove equity info while running than at app
   * shutdown. */
}

/* Class Init Functions */
void ClassInitPortfolioPacket() {
  /* Allocate Memory For A New Class Object */
  portfolio_packet *new_class =
      (portfolio_packet *)g_malloc(sizeof(*new_class));

  /* Initialize Variables */
  new_class->metal_class = ClassInitMetal();
  new_class->equity_folder_class = ClassInitEquityFolder();
  new_class->meta_class = ClassInitMeta();

  /* Connect Function Pointers To Function Definitions */
  new_class->Calculate = Calculate;
  new_class->ToStrings = ToStrings;
  new_class->GetData = GetData;
  new_class->ExtractData = ExtractData;
  new_class->IsFetchingData = IsFetchingData;
  new_class->SetFetchingData = SetFetchingData;
  new_class->IsDefaultView = IsDefaultView;
  new_class->SetDefaultView = SetDefaultView;
  new_class->FreeMainCurlData = FreeMainCurlData;
  new_class->StopMultiCurlMain = StopMultiCurlMain;
  new_class->StopMultiCurlAll = StopMultiCurlAll;
  new_class->IsCurlCanceled = IsCurlCanceled;
  new_class->SetCurlCanceled = SetCurlCanceled;
  new_class->IsMainCurlCanceled = IsMainCurlCanceled;
  new_class->SetMainCurlCanceled = SetMainCurlCanceled;
  new_class->GetHoursOfUpdates = GetHoursOfUpdates;
  new_class->GetUpdatesPerMinute = GetUpdatesPerMinute;
  new_class->GetPrimaryHeadings = GetPrimaryHeadings;
  new_class->GetDefaultHeadings = GetDefaultHeadings;
  new_class->SaveSqlData = SaveSqlData;
  new_class->GetWindowData = GetWindowData;
  new_class->GetMetaClass = GetMetaClass;
  new_class->GetMetalClass = GetMetalClass;
  new_class->GetEquityFolderClass = GetEquityFolderClass;
  new_class->GetSymNameMap = GetSymNameMap;
  new_class->SetSymNameMap = SetSymNameMap;
  new_class->IsClockDisplayed = IsClockDisplayed;
  new_class->SetClockDisplayed = SetClockDisplayed;
  new_class->IsIndicesDisplayed = IsIndicesDisplayed;
  new_class->SetIndicesDisplayed = SetIndicesDisplayed;
  new_class->SetSecurityNames = SetSecurityNames;
  new_class->SetClosed = SetClosed;
  new_class->IsClosed = IsClosed;

  /* General Multicurl Handle for the Main Fetch Operation */
  new_class->multicurl_main_hnd = curl_multi_init();

  /* Set the global variable. */
  packet = new_class;
}

/* Class Destruct Functions */
void ClassDestructPortfolioPacket(portfolio_packet *pkg) {
  /* Free Memory From Class Member Objects */
  if (pkg->equity_folder_class)
    ClassDestructEquityFolder(pkg->equity_folder_class);
  if (pkg->metal_class)
    ClassDestructMetal(pkg->metal_class);
  if (pkg->meta_class)
    ClassDestructMeta(pkg->meta_class);
  if (pkg->multicurl_main_hnd)
    curl_multi_cleanup(pkg->multicurl_main_hnd);

  /* Free Memory From Class Object */
  g_free(pkg);
}