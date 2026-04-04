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
// Purpose: ID24 miscellaneous definitions and functions.
// Authors: Dron12261
//

#ifndef ID24_MISC_H_
#define ID24_MISC_H_

namespace id24
{
void ID24_Reset();
void ID24_Enable(const char *reason, const char *details);
bool ID24_IsEnabled();

// Checks for loaded WADs that contain ID24 content, and if found, enables ID24 features.
void ID24_DetectAndEnable();

// Best-effort loader for ID24 support wad.
void ID24_TryLoadId24Res();
} // namespace id24

#endif