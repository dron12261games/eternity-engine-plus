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

#include "z_zone.h"
#include "id24_sbardef.h"

#include <cstdio>

#include "d_main.h"
#include "doomstat.h"
#include "id24_json.h"
#include "id24_misc.h"

namespace id24
{

static constexpr int SBARDEF_VIRTUAL_WIDTH  = 320;
static constexpr int SBARDEF_VIRTUAL_HEIGHT = 200;

static bool g_hasSBARDEF = false;
static int  g_barIndex   = 0;

static int g_lastScreenBlocks = -1;

static int g_statusbarCount = 0;

static int g_numberFontCount     = 0;
static int g_totalElementCount   = 0;
static int g_totalConditionCount = 0;

static unsigned int g_runtimeTics = 0;

static bool g_drawStubAnnounced  = false;
static int  g_lastLoggedBarIndex = -1;

// Cached counts for currently active statusbar (barindex).
static int  g_activeElementCount   = 0;
static int  g_activeConditionCount = 0;
static bool g_activeCacheValid     = false;

static int g_runtimeCandidateElementCount = 0;
static int g_runtimeEligibleElementCount  = 0;

// Keep validated SBARDEF data for runtime queries/next phases.
static nlohmann::json g_sbardefData;

// Warning function for JSON parsing: forwards to usermsg with SBARDEF context
static void warningFunc(bool error, const char *msg)
{
    usermsg("SBARDEF %s: %s", error ? "ERROR" : "WARN", msg ? msg : "(null)");
}

// Returns pointer to active statusbar object or nullptr if unavailable
static const nlohmann::json *ID24_GetActiveStatusBarDef()
{
    if(!g_hasSBARDEF || !g_sbardefData.is_object())
        return nullptr;

    if(!g_sbardefData.contains("statusbars") || !g_sbardefData["statusbars"].is_array())
        return nullptr;

    const auto &statusbars = g_sbardefData["statusbars"];
    if(g_barIndex < 0 || g_barIndex >= static_cast<int>(statusbars.size()))
        return nullptr;

    const auto &bar = statusbars[g_barIndex];
    if(!bar.is_object())
        return nullptr;

    return &bar;
}

// Rebuild runtime candidate set for current tick (Phase-2 scaffold).
// Current rule:
// - candidate: every element of active bar
// - eligible: only elements without local conditions (or with empty conditions array)
static void ID24_RebuildRuntimeCandidateCache()
{
    g_runtimeCandidateElementCount = 0;
    g_runtimeEligibleElementCount  = 0;

    if(!ID24_HasActiveStatusBar())
        return;

    const nlohmann::json *bar = ID24_GetActiveStatusBarDef();
    if(!bar || !bar->contains("elements") || !(*bar)["elements"].is_array())
        return;

    const auto &elements           = (*bar)["elements"];
    g_runtimeCandidateElementCount = static_cast<int>(elements.size());

    for(const auto &element : elements)
    {
        if(!element.is_object())
            continue;

        // No local conditions => eligible
        if(!element.contains("conditions"))
        {
            ++g_runtimeEligibleElementCount;
            continue;
        }

        // Local conditions exist: only empty-array conditions are treated as eligible in scaffold
        if(element["conditions"].is_array() && element["conditions"].empty())
            ++g_runtimeEligibleElementCount;
    }
}

// Rebuild cached counters for currently active statusbar.
static void ID24_RebuildActiveBarCache()
{
    g_activeElementCount   = 0;
    g_activeConditionCount = 0;

    const nlohmann::json *bar = ID24_GetActiveStatusBarDef();
    if(!bar)
    {
        g_activeCacheValid = true;
        return;
    }

    if(bar->contains("elements") && (*bar)["elements"].is_array())
        g_activeElementCount = static_cast<int>((*bar)["elements"].size());

    if(bar->contains("conditions") && (*bar)["conditions"].is_array())
        g_activeConditionCount = static_cast<int>((*bar)["conditions"].size());

    g_activeCacheValid = true;
}

// JSON lump parsing function for SBARDEF: validates structure and content of the JSON data
static jsonLumpResult_e parseFunc(const nlohmann::json &data, void *, jsonWarning_t warning)
{
    if(!data.is_object())
    {
        warning(true, "data must be an object");
        return JLR_INVALID;
    }

    if(!data.contains("statusbars") || !data["statusbars"].is_array())
    {
        warning(true, "missing or invalid 'statusbars' array");
        return JLR_INVALID;
    }

    if(data["statusbars"].empty())
    {
        warning(true, "'statusbars' array is empty");
        return JLR_INVALID;
    }

    const auto &statusbars = data["statusbars"];

    int totalElements   = 0;
    int totalConditions = 0;

    for(size_t i = 0; i < statusbars.size(); ++i)
    {
        const auto &bar = statusbars[i];

        if(!bar.is_object())
        {
            char msg[128];
            std::snprintf(msg, sizeof(msg), "statusbars[%zu] must be an object", i);
            warning(true, msg);
            return JLR_INVALID;
        }

        if(bar.contains("elements"))
        {
            if(!bar["elements"].is_array())
            {
                char msg[128];
                std::snprintf(msg, sizeof(msg), "statusbars[%zu].elements must be an array", i);
                warning(true, msg);
                return JLR_INVALID;
            }

            const auto &elements  = bar["elements"];
            totalElements        += static_cast<int>(elements.size());

            for(size_t j = 0; j < elements.size(); ++j)
            {
                if(!elements[j].is_object())
                {
                    char msg[128];
                    std::snprintf(msg, sizeof(msg), "statusbars[%zu].elements[%zu] must be an object", i, j);
                    warning(true, msg);
                    return JLR_INVALID;
                }
            }
        }

        if(bar.contains("conditions"))
        {
            if(!bar["conditions"].is_array())
            {
                char msg[128];
                std::snprintf(msg, sizeof(msg), "statusbars[%zu].conditions must be an array", i);
                warning(true, msg);
                return JLR_INVALID;
            }

            const auto &conditions  = bar["conditions"];
            totalConditions        += static_cast<int>(conditions.size());

            for(size_t j = 0; j < conditions.size(); ++j)
            {
                if(!conditions[j].is_object())
                {
                    char msg[128];
                    std::snprintf(msg, sizeof(msg), "statusbars[%zu].conditions[%zu] must be an object", i, j);
                    warning(true, msg);
                    return JLR_INVALID;
                }
            }
        }
    }

    int numberFontCount = 0;

    if(data.contains("numberfonts"))
    {
        if(!data["numberfonts"].is_object())
        {
            warning(true, "numberfonts must be an object");
            return JLR_INVALID;
        }

        const auto &numberfonts = data["numberfonts"];
        numberFontCount         = static_cast<int>(numberfonts.size());

        for(auto it = numberfonts.begin(); it != numberfonts.end(); ++it)
        {
            if(!it.value().is_object())
            {
                char msg[128];
                std::snprintf(msg, sizeof(msg), "numberfonts.%s must be an object", it.key().c_str());
                warning(true, msg);
                return JLR_INVALID;
            }
        }
    }

    g_statusbarCount      = static_cast<int>(statusbars.size());
    g_numberFontCount     = numberFontCount;
    g_totalElementCount   = totalElements;
    g_totalConditionCount = totalConditions;

    g_sbardefData = data;

    return JLR_OK;
}

// Resets SBARDEF state to legacy defaults (no SBARDEF loaded)
void ID24_ResetSBARDEF()
{
    g_hasSBARDEF                   = false;
    g_barIndex                     = 0;
    g_lastScreenBlocks             = -1;
    g_statusbarCount               = 0;
    g_numberFontCount              = 0;
    g_totalElementCount            = 0;
    g_totalConditionCount          = 0;
    g_runtimeTics                  = 0;
    g_drawStubAnnounced            = false;
    g_lastLoggedBarIndex           = -1;
    g_activeElementCount           = 0;
    g_activeConditionCount         = 0;
    g_activeCacheValid             = false;
    g_runtimeCandidateElementCount = 0;
    g_runtimeEligibleElementCount  = 0;
    g_sbardefData                  = nlohmann::json();
}

// 35 tics runtime update hook for SBARDEF logic
void ID24_Ticker()
{
    if(!g_hasSBARDEF)
        return;

    ++g_runtimeTics;

    // 35 tics runtime pass scaffold
    ID24_RebuildRuntimeCandidateCache();
}

// Returns whether a valid SBARDEF was loaded, indicating that
// the custom status bar should be used instead of the legacy one
bool ID24_HasSBARDEF()
{
    return g_hasSBARDEF;
}

// Returns true if SBARDEF is loaded and there is at least one selectable statusbar definition
bool ID24_HasActiveStatusBar()
{
    return g_hasSBARDEF && g_statusbarCount > 0;
}

// Returns number of parsed statusbars from active SBARDEF (0 if none)
int ID24_GetSBARDEFStatusBarCount()
{
    return g_statusbarCount;
}

// Returns parsed numberfonts count from active SBARDEF (0 if none)
int ID24_GetSBARDEFNumberFontCount()
{
    return g_numberFontCount;
}

// Returns total parsed elements count from active SBARDEF (0 if none)
int ID24_GetSBARDEFTotalElementCount()
{
    return g_totalElementCount;
}

// Returns total parsed conditions count from active SBARDEF (0 if none)
int ID24_GetSBARDEFTotalConditionCount()
{
    return g_totalConditionCount;
}

// Returns number of elements in currently active statusbar definition 
// (0 if no active bar or invalid data)
int ID24_GetSBARDEFActiveElementCount()
{
    return g_activeElementCount;
}

// Returns number of conditions in currently active statusbar definition 
// (0 if no active bar or invalid data)
int ID24_GetSBARDEFActiveConditionCount()
{
    return g_activeConditionCount;
}

// Returns number of runtime candidate elements in currently active statusbar 
// definition (0 if no active bar or invalid data)
int ID24_GetSBARDEFRuntimeCandidateElementCount()
{
    return g_runtimeCandidateElementCount;
}

// Returns number of runtime eligible elements in currently active statusbar definition
int ID24_GetSBARDEFRuntimeEligibleElementCount()
{
    return g_runtimeEligibleElementCount;
}

// Returns current SBARDEF runtime tics (35Hz), 0 if inactive
unsigned int ID24_GetSBARDEFRuntimeTics()
{
    return g_runtimeTics;
}

// Returns the virtual width of the SBARDEF coordinate space (constant)
int ID24_GetSBARDEFVirtualWidth()
{
    return SBARDEF_VIRTUAL_WIDTH;
}

// Returns the virtual height of the SBARDEF coordinate space (constant)
int ID24_GetSBARDEFVirtualHeight()
{
    return SBARDEF_VIRTUAL_HEIGHT;
}

// Spec rule: barindex = max(screenblocks - 10, 0)
void ID24_UpdateSBARDEFBarIndex(int screenblocks)
{
    g_lastScreenBlocks = screenblocks;

    if(!ID24_HasActiveStatusBar())
    {
        if(g_barIndex != 0 || !g_activeCacheValid)
        {
            g_barIndex = 0;
            ID24_RebuildActiveBarCache();
        }

        ID24_RebuildRuntimeCandidateCache();
        return;
    }

    int barindex = (screenblocks > 10) ? (screenblocks - 10) : 0;

    if(barindex >= g_statusbarCount)
        barindex = g_statusbarCount - 1;

    if(g_barIndex != barindex || !g_activeCacheValid)
    {
        g_barIndex = barindex;
        ID24_RebuildActiveBarCache();
    }

    ID24_RebuildRuntimeCandidateCache();
}

// Returns the current SBARDEF bar index, which determines which
// status bar definition to use based on screenblocks
int ID24_GetSBARDEFBarIndex()
{
    return g_barIndex;
}

// Runtime hook for SBARDEF drawing: returns true if SBARDEF handled the draw
// call and legacy status bar should be skipped
bool ID24_DrawSBARDEF(bool fullscreen, bool fshud)
{
    if(!ID24_HasActiveStatusBar())
        return false;

    if(fullscreen && !fshud)
        return false;

    const int curBarIndex = ID24_GetSBARDEFBarIndex();

    if(!g_drawStubAnnounced)
    {
        usermsg("SBARDEF: draw stub active (virtual=%dx%d, screenblocks=%d, barindex=%d, statusbars=%d, "
                "active elements=%d, active conditions=%d, runtime candidates=%d, runtime eligible=%d, "
                "numberfonts=%d, elements=%d, conditions=%d, tics=%u), using legacy renderer",
                ID24_GetSBARDEFVirtualWidth(), ID24_GetSBARDEFVirtualHeight(), g_lastScreenBlocks, curBarIndex,
                ID24_GetSBARDEFStatusBarCount(), ID24_GetSBARDEFActiveElementCount(),
                ID24_GetSBARDEFActiveConditionCount(), g_runtimeCandidateElementCount, g_runtimeEligibleElementCount,
                ID24_GetSBARDEFNumberFontCount(), ID24_GetSBARDEFTotalElementCount(),
                ID24_GetSBARDEFTotalConditionCount(), ID24_GetSBARDEFRuntimeTics());

        g_drawStubAnnounced  = true;
        g_lastLoggedBarIndex = curBarIndex;
    }
    else if(curBarIndex != g_lastLoggedBarIndex)
    {
        usermsg("SBARDEF: active barindex changed to %d (screenblocks=%d, active elements=%d, active conditions=%d, "
                "runtime candidates=%d, runtime eligible=%d, tics=%u)",
                curBarIndex, g_lastScreenBlocks, ID24_GetSBARDEFActiveElementCount(),
                ID24_GetSBARDEFActiveConditionCount(), g_runtimeCandidateElementCount, g_runtimeEligibleElementCount,
                ID24_GetSBARDEFRuntimeTics());

        g_lastLoggedBarIndex = curBarIndex;
    }

    return false;
}

// Loads and parses the SBARDEF JSON lump, updating global state accordingly
void ID24_LoadSBARDEF()
{
    ID24_ResetSBARDEF();

    // Non-ID24 path: stay silent and keep legacy status bar.
    if(!ID24_IsEnabled())
        return;

    const jsonLumpResult_e result =
        ParseJSONLumpByName("SBARDEF", "statusbar", { 1, 0, 0 }, parseFunc, nullptr, warningFunc);

    if(result == JLR_NO_LUMP)
    {
        usermsg("SBARDEF: not found (using legacy status bar)");
        return;
    }

    if(result == JLR_OK)
    {
        g_hasSBARDEF = true;

        // Initialize active bar index immediately from current view size.
        ID24_UpdateSBARDEFBarIndex(screenSize); // also rebuilds active cache

        usermsg("SBARDEF: loaded (%d statusbars, %d numberfonts, %d elements, %d conditions, initial barindex=%d, "
                "active elements=%d, active conditions=%d)",
                ID24_GetSBARDEFStatusBarCount(), ID24_GetSBARDEFNumberFontCount(), ID24_GetSBARDEFTotalElementCount(),
                ID24_GetSBARDEFTotalConditionCount(), ID24_GetSBARDEFBarIndex(), ID24_GetSBARDEFActiveElementCount(),
                ID24_GetSBARDEFActiveConditionCount());
        return;
    }

    usermsg("SBARDEF: rejected (using legacy status bar)");
}

} // namespace id24