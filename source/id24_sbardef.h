//
// The Eternity Engine
// Copyright (C) 2025 James Haley et al.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
// Additional terms and conditions compatible with the GPLv3 apply. See the
// file COPYING-EE for details.
//
//------------------------------------------------------------------------------
//
// Purpose: ID24 SBARDEF support 
// Authors: Dron12261
//

#ifndef ID24_SBARDEF_H_
#define ID24_SBARDEF_H_

namespace id24
{

void ID24_ResetSBARDEF();
void ID24_LoadSBARDEF();
void ID24_Ticker();

bool ID24_HasSBARDEF();
bool ID24_HasActiveStatusBar();

int ID24_GetSBARDEFStatusBarCount();
int ID24_GetSBARDEFNumberFontCount();
int ID24_GetSBARDEFTotalElementCount();
int ID24_GetSBARDEFTotalConditionCount();

int ID24_GetSBARDEFActiveElementCount();
int ID24_GetSBARDEFActiveConditionCount();

int ID24_GetSBARDEFRuntimeCandidateElementCount();
int ID24_GetSBARDEFRuntimeEligibleElementCount();

unsigned int ID24_GetSBARDEFRuntimeTics();

int ID24_GetSBARDEFVirtualWidth();
int ID24_GetSBARDEFVirtualHeight();

void ID24_UpdateSBARDEFBarIndex(int screenblocks);
int  ID24_GetSBARDEFBarIndex();

bool ID24_DrawSBARDEF(bool fullscreen, bool fshud);

} // namespace id24

#endif