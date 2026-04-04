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
// Purpose: ID24 GAMECONF format
// Authors: Dron12261
//

#ifndef ID24_GAMECONF_H_
#define ID24_GAMECONF_H_

#include <cstddef>

namespace id24
{

// Loads the GAMECONF lump chain and processes its contents, if present.
// This should be called early in the startup process, before IWAD detection and autoloads
void ID24_LoadGameconfEarly();

// Accessors for GAMECONF state
bool        ID24_GameconfHasIwad();
const char *ID24_GameconfIwad();

// Returns the number of PWADs specified in GAMECONF, or 0 if none
size_t      ID24_GameconfNumPwads();
const char *ID24_GameconfPwadAt(size_t index);

// Returns whether GAMECONF specified any options, and if so, returns the options string. 
// The format of the options string is not defined by the engine, it's just passed to 
// IWADs and mods that support it.
bool        ID24_GameconfHasOptions();
const char *ID24_GameconfOptions();

// Returns whether GAMECONF specified any player translations, and if so, allows access to them.
bool        ID24_GameconfHasPlayerTranslations();
size_t      ID24_GameconfNumPlayerTranslations();
const char *ID24_GameconfPlayerTranslationAt(size_t index);

} // namespace id24

#endif