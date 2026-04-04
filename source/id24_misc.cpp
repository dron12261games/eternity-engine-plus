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

#include "z_zone.h"
#include "id24_misc.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

#include "d_files.h"
#include "d_gi.h"
#include "d_main.h"
#include "d_deh.h"
#include "id24_json.h"
#include "w_wad.h"
#include "z_auto.h"

namespace id24
{
static bool        g_id24Enabled      = false;
static char       *g_reason           = nullptr;
static char       *g_details          = nullptr;
static const char *g_currentProbeLump = nullptr;

// Frees the reason and details strings, and sets their pointers to nullptr
static void ID24_FreeStrings()
{
    if(g_reason)
    {
        efree(g_reason);
        g_reason = nullptr;
    }
    if(g_details)
    {
        efree(g_details);
        g_details = nullptr;
    }
}

// Resets ID24 state to default (disabled, no reason/details)
void ID24_Reset()
{
    g_id24Enabled = false;
    ID24_FreeStrings();
}

// Enables ID24 features, with a reason and details for logging purposes. If already enabled, does nothing
void ID24_Enable(const char *reason, const char *details)
{
    if(g_id24Enabled)
        return; // already enabled, ignore

    g_id24Enabled = true;
    g_reason      = estrdup(reason ? reason : "unknown");
    g_details     = estrdup(details ? details : "none");
    usermsg("ID24: enabled (reason=%s, details=%s)", g_reason, g_details);
}

// Returns whether ID24 features are enabled
bool ID24_IsEnabled()
{
    return g_id24Enabled;
}

// Dummy parse function for probing JSON lumps; we just want to check if it parses successfully, 
// we don't need to actually do anything with the data
static jsonLumpResult_e ID24_DummyParse(const nlohmann::json &, void *, jsonWarning_t)
{
    return JLR_OK;
}

// Warning function for probing JSON lumps; logs the message with the lump name 
// (or "JSON" if not known) and whether it's an error or warning
static void ID24_ProbeWarning(bool error, const char *msg)
{
    usermsg("ID24: %s: %s%s", g_currentProbeLump ? g_currentProbeLump : "JSON",
            error ? "error: " : "warning: ", msg ? msg : "(null)");
}

// Checks if a lump with the given name exists and contains valid JSON of the expected type
static bool ID24_HasValidJSONLump(const char *lumpName, const char *expectedType)
{
    g_currentProbeLump = lumpName;

    const jsonLumpResult_e r =
        ParseJSONLumpByName(lumpName, expectedType, { 1, 0, 0 }, ID24_DummyParse, nullptr, ID24_ProbeWarning);

    g_currentProbeLump = nullptr;

    return r == JLR_OK;
}

// Checks if a DEHACKED lump exists that contains the string "Doom version" and "2024", 
// indicating it's a DEHACKED patch for Doom version 2024.
static bool ID24_DehHasDoomVersion2024()
{
    if(wGlobalDir.checkNumForName("DEHACKED") < 0)
        return false;

    ZAutoBuffer buf;
    wGlobalDir.cacheLumpAuto("DEHACKED", buf);
    if(!buf.get() || !buf.getSize())
        return false;

    return D_DEHHasDoomVersion(buf.get(), buf.getSize(), 2024);
}

// Probes loaded WADs for signals that indicate ID24 features should be enabled, and enables them if found.
void ID24_DetectAndEnable()
{
    if(g_id24Enabled)
        return;

    if(ID24_HasValidJSONLump("SBARDEF", "statusbar"))
    {
        ID24_Enable("json-lump", "SBARDEF/statusbar");
        return;
    }
    if(ID24_HasValidJSONLump("SKYDEFS", "skydefs"))
    {
        ID24_Enable("json-lump", "SKYDEFS/skydefs");
        return;
    }
    if(ID24_HasValidJSONLump("DEMOLOOP", "demoloop"))
    {
        ID24_Enable("json-lump", "DEMOLOOP/demoloop");
        return;
    }
    if(ID24_DehHasDoomVersion2024())
    {
        ID24_Enable("dehacked", "Doom version 2024");
        return;
    }

    usermsg("ID24: not enabled (no ID24 signals found)");
}

void ID24_TryLoadId24Res()
{
    if(!ID24_IsEnabled())
        return;

    // Check if the path is set
    if(!gi_path_id24res || !*gi_path_id24res)
    {
        usermsg("ID24: id24res.wad not found; some resources may be missing");
        return;
    }

    // Prevent duplicate load if already present.
    if(wGlobalDir.checkNumForLFN("id24res.wad") >= 0 || wGlobalDir.checkNumForLFN(gi_path_id24res) >= 0)
        return;

    // Try to load the wad. If it fails, log a message but don't error out, 
    // since ID24 can still function without it (albeit with missing resources).
    if(!wGlobalDir.addNewFile(gi_path_id24res))
    {
        usermsg("ID24: failed to load id24res.wad from %s", gi_path_id24res);
        return;
    }

    // Keep startup wad list in sync.
    D_AddFile(gi_path_id24res, lumpinfo_t::ns_global, nullptr, 0, DAF_NONE);

    usermsg("ID24: id24res.wad loaded from %s", gi_path_id24res);
}

} // namespace id24