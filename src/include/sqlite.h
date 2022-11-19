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

#ifndef SQLITE_HEADER_H
#define SQLITE_HEADER_H

#include "gui_types.h"       /* window_data */
#include "class_types.h"     /* equity_folder, metal, meta */

void SqliteProcessing (equity_folder*,metal*,meta*,window_data*);

void SqliteAddEquity (char*,char*,meta*);
void SqliteRemoveEquity (char*,meta*);
void SqliteRemoveAllEquity (meta*);

void SqliteAddBullion (char*,char*,char*,metal*,meta*);
void SqliteAddCash (char*,meta*);
void SqliteAddAPIData (char*,char*,meta*);

void SqliteAddMainWindowSize (int,int,meta*);
void SqliteAddMainWindowPos (int,int,meta*);
void SqliteAddRSIWindowSize (int,int,meta*);
void SqliteAddRSIWindowPos (int,int,meta*);

#endif /* SQLITE_HEADER_H */