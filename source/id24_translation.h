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
// Purpose: ID24 Translation support
// Authors: Dron12261
//

#ifndef ID24_TRANSLATION_H_
#define ID24_TRANSLATION_H_

#include "doomtype.h"
#include <cstddef>

namespace id24
{

// Loads all available ID24 translations. This should be called at startup,
// after WADs are loaded but before EDF processing
void ID24_LoadTranslations();

// Returns true if a translation with the given name is loaded and available for use
bool ID24_HasTranslation(const char *name);

// Returns a pointer to the translation table for the given name, or nullptr if not found
const byte *ID24_GetTranslation(const char *name);

// Returns a pointer to the translation table for the wadtranslation, or nullptr if not found
const byte *ID24_GetWadTranslation();

// Returns the number of entries in the wadtranslation table, or 0 if not found
int ID24_GetWadTranslationNum();

// Returns a pointer to the translation table for the given player index (0-3),
// or nullptr if not found or index out of range
const byte *ID24_GetPlayerTranslation(size_t playerIndex);

// Returns the number of entries in the player translation table for the given player
// index (0-3), or 0 if not found or index out of range
int ID24_GetPlayerTranslationNum(size_t playerIndex);

// Applies active GAMECONF wadtranslation to a byte buffer in-place.
// No-op if there is no active wadtranslation or buffer is null.
void ID24_ApplyWadTranslationToBuffer(byte *data, size_t size);

} // namespace id24

#endif