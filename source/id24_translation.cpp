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

#include "z_zone.h"
#include "id24_translation.h"

#include <string>
#include <vector>

#include "doomtype.h"
#include "d_main.h"
#include "id24_gameconf.h"
#include "id24_misc.h"
#include "v_video.h"
#include "w_wad.h"
#include "r_draw.h"

namespace id24
{

struct translation_entry_t
{
    std::string name;
    byte       *table;
};

static std::vector<translation_entry_t> g_translations;
static const byte                      *g_wadTranslation           = nullptr;
static int                              g_wadTranslationNum        = 0;
static const byte                      *g_playerTranslations[4]    = { nullptr, nullptr, nullptr, nullptr };
static int                              g_playerTranslationNums[4] = { 0, 0, 0, 0 };

// Preserve default translation slots so we can restore them when ID24 is disabled/reloaded.
static byte *g_defaultTranslationSlots[CR_MAXCUSTOM + 1] = {};
static bool  g_defaultTranslationSlotsCaptured           = false;

// Captures default translation slots once.
static void captureDefaultTranslationSlots()
{
    if(g_defaultTranslationSlotsCaptured)
        return;

    for(int i = 0; i <= CR_MAXCUSTOM; ++i)
        g_defaultTranslationSlots[i] = colrngs[i];

    g_defaultTranslationSlotsCaptured = true;
}

// Restores translation slots to defaults captured from startup state.
static void restoreDefaultTranslationSlots()
{
    captureDefaultTranslationSlots();

    for(int i = 0; i <= CR_MAXCUSTOM; ++i)
        colrngs[i] = g_defaultTranslationSlots[i];
}

// Finds the index of a translation by name, or returns -1 if not found
static int findTranslationIndex(const char *name)
{
    if(!name || !*name)
        return -1;

    for(size_t i = 0; i < g_translations.size(); i++)
    {
        if(!strcasecmp(g_translations[i].name.c_str(), name))
            return static_cast<int>(i);
    }

    return -1;
}

// Applies translation override to an existing engine translation slot by name,
// if such slot exists.
static void applyKnownTranslationOverride(const char *name, byte *table)
{
    if(!name || !*name || !table)
        return;

    const int trnum = R_TranslationNumForName(name);
    if(trnum > 0 && trnum <= CR_MAXCUSTOM)
        colrngs[trnum] = table;
}

// Registers a translation table by name, replacing any existing entry with the same name
static void registerTranslation(const char *name, byte *table)
{
    const int idx = findTranslationIndex(name);
    if(idx >= 0)
    {
        g_translations[idx].table = table;
        applyKnownTranslationOverride(name, table);
        return;
    }

    g_translations.push_back({ name, table });
    applyKnownTranslationOverride(name, table);
}

// Attempts to load a translation lump by name, and if successful, registers it. Logs warnings on failure
static void tryLoadTranslationLump(const char *name, const char *sourceTag)
{
    if(!name || !*name)
        return;

    const int lump = wGlobalDir.checkNumForNameNSG(name, lumpinfo_t::ns_translations);
    if(lump < 0)
    {
        usermsg("ID24 TRANSLATION WARN: %s '%s' not found", sourceTag, name);
        return;
    }

    const int lumpSize = wGlobalDir.lumpLength(lump);
    if(lumpSize != 256)
    {
        usermsg("ID24 TRANSLATION WARN: %s '%s' has invalid size %d (expected 256)", sourceTag, name, lumpSize);
        return;
    }

    byte *table = static_cast<byte *>(wGlobalDir.cacheLumpNum(lump, PU_STATIC, nullptr));
    registerTranslation(name, table);

    lumpinfo_t **lumpInfo          = wGlobalDir.getLumpInfo();
    const bool   fromTranslationNS = lumpInfo[lump] && lumpInfo[lump]->li_namespace == lumpinfo_t::ns_translations;

    usermsg("ID24 TRANSLATION loaded: %s (%s, ns=%s)", name, sourceTag, fromTranslationNS ? "translations" : "global");
}

// Loads all valid translations from ns_translations into the runtime registry.
// Later lumps with the same name replace earlier ones (WAD order semantics).
static void loadAllNamespaceTranslations()
{
    lumpinfo_t **lumpInfo = wGlobalDir.getLumpInfo();
    const int    numLumps = wGlobalDir.getNumLumps();

    for(int i = 0; i < numLumps; ++i)
    {
        lumpinfo_t *li = lumpInfo[i];
        if(!li || li->li_namespace != lumpinfo_t::ns_translations)
            continue;

        const int lumpSize = wGlobalDir.lumpLength(i);
        if(lumpSize != 256)
        {
            usermsg("ID24 TRANSLATION WARN: ns_translations lump '%s' has invalid size %d (expected 256)", li->name,
                    lumpSize);
            continue;
        }

        byte *table = static_cast<byte *>(wGlobalDir.cacheLumpNum(i, PU_STATIC, nullptr));
        registerTranslation(li->name, table);

        usermsg("ID24 TRANSLATION loaded: %s (scan, ns=translations)", li->name);
    }
}

// Applies the WAD translation from the game configuration to the global wad translation
// pointer, if defined and valid
static void applyGameconfWadTranslation()
{
    g_wadTranslation    = nullptr;
    g_wadTranslationNum = 0;

    if(!ID24_GameconfHasWadTranslation())
        return;

    const char *name = ID24_GameconfWadTranslation();
    const byte *tr   = ID24_GetTranslation(name);

    if(!tr)
    {
        usermsg("ID24 TRANSLATION WARN: wadtranslation '%s' is not loaded", name ? name : "(null)");
        return;
    }

    g_wadTranslationNum = R_TranslationNumForName(name);
    if(g_wadTranslationNum <= 0)
        usermsg("ID24 TRANSLATION WARN: wadtranslation '%s' not in ns_translations", name);

    g_wadTranslation = tr;
    usermsg("ID24 TRANSLATION applied: wadtranslation -> %s", name);
}

// Applies player translation tables from the game configuration to the custom color ranges,
// if they are defined and valid. Logs warnings for any issues encountered
static void applyGameconfPlayerTranslations()
{
    for(size_t i = 0; i < 4; i++)
        g_playerTranslations[i] = nullptr;
    for(size_t i = 0; i < 4; i++)
        g_playerTranslationNums[i] = 0;

    if(!ID24_GameconfHasPlayerTranslations())
        return;

    const size_t n = ID24_GameconfNumPlayerTranslations();
    if(n > 4)
        usermsg("ID24 TRANSLATION WARN: playertranslations has %zu entries; only first 4 are used", n);

    for(size_t i = 0; i < 4 && i < n; i++)
    {
        const char *name = ID24_GameconfPlayerTranslationAt(i);
        if(!name || !*name)
            continue;

        const byte *tr = ID24_GetTranslation(name);
        if(!tr)
        {
            usermsg("ID24 TRANSLATION WARN: playertranslation '%s' is not loaded", name);
            continue;
        }

        g_playerTranslationNums[i] = R_TranslationNumForName(name);
        if(g_playerTranslationNums[i] <= 0)
            usermsg("ID24 TRANSLATION WARN: playertranslation '%s' not in ns_translations", name);

        g_playerTranslations[i] = tr;

        const int slot = CR_CUSTOM1 + static_cast<int>(i);
        if(slot <= CR_MAXCUSTOM)
        {
            colrngs[slot]              = const_cast<byte *>(tr);
            g_playerTranslationNums[i] = slot;
            usermsg("ID24 TRANSLATION applied: player %zu -> %s", i, name);
        }
    }
}

// Loads translation tables from the WAD based on game configuration, and registers them for use.
// Logs warnings for any issues encountered
void ID24_LoadTranslations()
{
    g_translations.clear();
    g_wadTranslation    = nullptr;
    g_wadTranslationNum = 0;
    for(size_t i = 0; i < 4; i++)
        g_playerTranslations[i] = nullptr;
    for(size_t i = 0; i < 4; i++)
        g_playerTranslationNums[i] = 0;

    // Always reset translation slots to defaults before any ID24 work.
    restoreDefaultTranslationSlots();

    // Non-ID24 path: keep vanilla/default translation state.
    if(!ID24_IsEnabled())
        return;

    loadAllNamespaceTranslations();

    if(ID24_GameconfHasWadTranslation())
        tryLoadTranslationLump(ID24_GameconfWadTranslation(), "wadtranslation");

    if(ID24_GameconfHasPlayerTranslations())
    {
        const size_t n = ID24_GameconfNumPlayerTranslations();
        for(size_t i = 0; i < n; i++)
        {
            const char *name = ID24_GameconfPlayerTranslationAt(i);
            if(name && *name)
                tryLoadTranslationLump(name, "playertranslations");
        }
    }

    applyGameconfWadTranslation();
    applyGameconfPlayerTranslations();
}

// Checks if a translation with the given name is registered and available
bool ID24_HasTranslation(const char *name)
{
    return findTranslationIndex(name) >= 0;
}

// Retrieves a pointer to the translation table for the given name, or nullptr if not found
const byte *ID24_GetTranslation(const char *name)
{
    const int idx = findTranslationIndex(name);
    if(idx < 0)
        return nullptr;

    return g_translations[idx].table;
}

// Retrieves the currently active WAD translation table, or nullptr if none is active
const byte *ID24_GetWadTranslation()
{
    return g_wadTranslation;
}

// Retrieves the currently active player translation table for the given player index (0-3),
// or nullptr if none is active or index is out of range
const byte *ID24_GetPlayerTranslation(size_t playerIndex)
{
    if(playerIndex >= 4)
        return nullptr;

    return g_playerTranslations[playerIndex];
}

// Retrieves the translation number for the currently active WAD translation, or 0 if none is active
int ID24_GetWadTranslationNum()
{
    return g_wadTranslationNum;
}

// Retrieves the translation number for the currently active player translation for the given player
// index (0-3), or 0 if none is active or index is out of range
int ID24_GetPlayerTranslationNum(size_t playerIndex)
{
    if(playerIndex >= 4)
        return 0;

    return g_playerTranslationNums[playerIndex];
}

// Applies active GAMECONF wadtranslation to a byte buffer in-place.
// No-op if there is no active wadtranslation or buffer is null.
void ID24_ApplyWadTranslationToBuffer(byte *data, size_t size)
{
    if(!ID24_IsEnabled() || !data || !size || !g_wadTranslation)
        return;

    for(size_t i = 0; i < size; ++i)
        data[i] = g_wadTranslation[data[i]];
}

} // namespace id24