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

#include "z_zone.h"
#include "m_qstr.h"
#include "w_wad.h"
#include "z_auto.h"
#include "id24_gameconf.h"

#include <cctype>
#include <string>
#include <vector>

#include "d_main.h"
#include "id24_json.h"
#include "id24_misc.h"

namespace id24
{

// Warning function for JSON parsing, which logs the message with a
// "GAMECONF" prefix and indicates whether it's an error or a warning
static void warningFunc(bool error, const char *msg)
{
    usermsg("GAMECONF %s: %s", error ? "ERROR" : "WARN", msg ? msg : "(null)");
}

// Returns a lowercase copy of the input string
static std::string toLowerCopy(std::string s)
{
    for(char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Checks if the string contains any path characters ('/', '\\', ':')
static bool containsPathChars(const std::string &s)
{
    return s.find('/') != std::string::npos || s.find('\\') != std::string::npos || s.find(':') != std::string::npos;
}

// Enums for supported executable complevels
enum gameconf_executable_e
{
    GC_EXE_INVALID = -1,
    GC_EXE_DOOM19,
    GC_EXE_LIMITREMOVING,
    GC_EXE_BUGFIXED,
    GC_EXE_BOOM202,
    GC_EXE_COMPLEVEL9,
    GC_EXE_MBF,
    GC_EXE_MBF21,
    GC_EXE_MBF21EX,
    GC_EXE_ID24
};

// Enums for supported game modes
enum gameconf_mode_e
{
    GC_MODE_INVALID = -1,
    GC_MODE_REGISTERED,
    GC_MODE_RETAIL,
    GC_MODE_COMMERCIAL
};

// Global variables to track the maximum executable level and mode required by the GAMECONF data
static gameconf_executable_e g_maxExeLevel = GC_EXE_INVALID;
static gameconf_mode_e       g_maxMode     = GC_MODE_INVALID;

// Converts an executable enum value to a string for logging purposes
static const char *exeToStr(gameconf_executable_e v)
{
    switch(v)
    {
    case GC_EXE_DOOM19:        return "doom1.9";
    case GC_EXE_LIMITREMOVING: return "limitremoving";
    case GC_EXE_BUGFIXED:      return "bugfixed";
    case GC_EXE_BOOM202:       return "boom2.02";
    case GC_EXE_COMPLEVEL9:    return "complevel9";
    case GC_EXE_MBF:           return "mbf";
    case GC_EXE_MBF21:         return "mbf21";
    case GC_EXE_MBF21EX:       return "mbf21ex";
    case GC_EXE_ID24:          return "id24";
    default:                   return "unset";
    }
}

// Converts a mode enum value to a string for logging purposes
static const char *modeToStr(gameconf_mode_e v)
{
    switch(v)
    {
    case GC_MODE_REGISTERED: return "registered";
    case GC_MODE_RETAIL:     return "retail";
    case GC_MODE_COMMERCIAL: return "commercial";
    default:                 return "unset";
    }
}

// Struct to hold the parsed GAMECONF data and track which fields were present
struct gameconf_state_t
{
    std::string title;
    std::string author;
    std::string description;
    std::string version;

    std::string              iwad;
    std::vector<std::string> pwads;

    std::vector<std::string> playertranslations;
    std::string              wadtranslation;
    std::string              options;

    bool hasTitle              = false;
    bool hasAuthor             = false;
    bool hasDescription        = false;
    bool hasVersion            = false;
    bool hasIwad               = false;
    bool hasPlayerTranslations = false;
    bool hasWadTranslation     = false;
    bool hasOptions            = false;
};

// Global state for the GAMECONF data
static gameconf_state_t g_state;

// Resets the GAMECONF state to default values (empty strings, false flags, and invalid exe/mode)
static void resetGameconfState()
{
    g_state       = {};
    g_maxExeLevel = GC_EXE_INVALID;
    g_maxMode     = GC_MODE_INVALID;
}

// Applies a string field from the JSON data to the destination string and sets the
// corresponding flag if the field is present and not null
static void applyDestructiveString(const nlohmann::json &data, const char *field, std::string &dst, bool &hasFlag)
{
    if(!data.contains(field) || data[field].is_null())
        return;

    dst     = data[field].get<std::string>();
    hasFlag = true;
}

// Applies the "iwad" field from the JSON data to the global state, validating that it's a
// filename without path characters, and logs a warning if it's invalid
static void applyPwadsAdditive(const nlohmann::json &data)
{
    if(!data.contains("pwads") || data["pwads"].is_null())
        return;

    for(const auto &v : data["pwads"])
        g_state.pwads.push_back(v.get<std::string>());
}

// Applies the "options" field from the JSON data to the global state, concatenating it with
// a newline if there are existing options
static void applyOptionsAdditive(const nlohmann::json &data)
{
    if(!data.contains("options") || data["options"].is_null())
        return;

    const std::string next = data["options"].get<std::string>();
    if(g_state.hasOptions && !g_state.options.empty() && !next.empty())
        g_state.options += "\n";
    g_state.options    += next;
    g_state.hasOptions  = true;
}

// Applies the "playertranslations" field from the JSON data to the global state, replacing any
// existing translations, and logs a warning if the field is invalid
static void applyPlayerTranslationsDestructive(const nlohmann::json &data)
{
    if(!data.contains("playertranslations") || data["playertranslations"].is_null())
        return;

    g_state.playertranslations.clear();
    for(const auto &v : data["playertranslations"])
        g_state.playertranslations.push_back(v.get<std::string>());

    g_state.hasPlayerTranslations = true;
}

// Parses the "executable" field from the JSON, returning the corresponding enum value,
// or GC_EXE_INVALID if it's not recognized
static gameconf_executable_e parseExecutable(const std::string &exe)
{
    const std::string v = toLowerCopy(exe);

    if(v == "doom1.9")
        return GC_EXE_DOOM19;
    if(v == "limitremoving")
        return GC_EXE_LIMITREMOVING;
    if(v == "bugfixed")
        return GC_EXE_BUGFIXED;
    if(v == "boom2.02")
        return GC_EXE_BOOM202;
    if(v == "complevel9")
        return GC_EXE_COMPLEVEL9;
    if(v == "mbf")
        return GC_EXE_MBF;
    if(v == "mbf21")
        return GC_EXE_MBF21;
    if(v == "mbf21ex")
        return GC_EXE_MBF21EX;
    if(v == "id24")
        return GC_EXE_ID24;

    return GC_EXE_INVALID;
}

// Parses the "mode" field from the JSON, returning the corresponding enum value,
// or GC_MODE_INVALID if it's not recognized
static gameconf_mode_e parseMode(const std::string &mode)
{
    const std::string v = toLowerCopy(mode);

    if(v == "registered")
        return GC_MODE_REGISTERED;
    if(v == "retail")
        return GC_MODE_RETAIL;
    if(v == "commercial")
        return GC_MODE_COMMERCIAL;

    return GC_MODE_INVALID;
}

// Validates that a filename string does not contain any path characters, and logs a warning if it does
static bool validateFilenameOnly(const char *fieldName, const std::string &value, jsonWarning_t warning)
{
    if(containsPathChars(value))
    {
        qstring msg;
        msg.Printf(128, "'%s' must not contain path", fieldName);
        warning(true, msg.constPtr());
        return false;
    }
    return true;
}

// Validates that a JSON value is an array of strings, and that each string does not contain any path characters.
// Logs warnings for any issues found.
static bool validateStringArrayFilenameOnly(const nlohmann::json &arr, const char *fieldName, jsonWarning_t warning)
{
    if(!arr.is_array())
    {
        qstring msg;
        msg.Printf(128, "'%s' must be array or null", fieldName);
        warning(true, msg.constPtr());
        return false;
    }

    for(const auto &v : arr)
    {
        if(!v.is_string())
        {
            qstring msg;
            msg.Printf(128, "'%s' entries must be strings", fieldName);
            warning(true, msg.constPtr());
            return false;
        }

        if(!validateFilenameOnly(fieldName, v.get<std::string>(), warning))
            return false;
    }

    return true;
}

// Validates that a JSON value is either a string or null, and logs a warning if it's not
static bool validateStringOrNullField(const nlohmann::json &data, const char *fieldName, jsonWarning_t warning)
{
    if(!data.contains(fieldName) || data[fieldName].is_null())
        return true;

    if(!data[fieldName].is_string())
    {
        qstring msg;
        msg.Printf(128, "'%s' must be string or null", fieldName);
        warning(true, msg.constPtr());
        return false;
    }

    return true;
}

// Parses the GAMECONF lump, and if it contains "executable" set to "id24", enables ID24 features.
static jsonLumpResult_e parseFunc(const nlohmann::json &data, void *, jsonWarning_t warning)
{
    if(!data.is_object())
    {
        warning(true, "data must be an object");
        return JLR_INVALID;
    }

    // Validate string or null fields
    if(!validateStringOrNullField(data, "title", warning) || !validateStringOrNullField(data, "author", warning) ||
       !validateStringOrNullField(data, "description", warning) ||
       !validateStringOrNullField(data, "version", warning) ||
       !validateStringOrNullField(data, "wadtranslation", warning) ||
       !validateStringOrNullField(data, "options", warning))
        return JLR_INVALID;

    // executable key check
    if(data.contains("executable") && !data["executable"].is_null())
    {
        if(!data["executable"].is_string())
        {
            warning(true, "'executable' must be string or null");
            return JLR_INVALID;
        }

        const gameconf_executable_e exeLevel = parseExecutable(data["executable"].get<std::string>());
        if(exeLevel == GC_EXE_INVALID)
        {
            warning(true, "unsupported 'executable' value");
            return JLR_INVALID;
        }

        if(exeLevel > g_maxExeLevel)
            g_maxExeLevel = exeLevel;
    }

    // mode key check (validate only for now)
    if(data.contains("mode") && !data["mode"].is_null())
    {
        if(!data["mode"].is_string())
        {
            warning(true, "'mode' must be string or null");
            return JLR_INVALID;
        }

        const gameconf_mode_e modeLevel = parseMode(data["mode"].get<std::string>());
        if(modeLevel == GC_MODE_INVALID)
        {
            warning(true, "unsupported 'mode' value");
            return JLR_INVALID;
        }

        if(modeLevel > g_maxMode)
            g_maxMode = modeLevel;
    }

    // iwad key check (must be filename only)
    if(data.contains("iwad") && !data["iwad"].is_null())
    {
        if(!data["iwad"].is_string())
        {
            warning(true, "'iwad' must be string or null");
            return JLR_INVALID;
        }

        if(!validateFilenameOnly("iwad", data["iwad"].get<std::string>(), warning))
            return JLR_INVALID;
    }

    // pwads key check (must be array of filename only)
    if(data.contains("pwads") && !data["pwads"].is_null())
    {
        if(!validateStringArrayFilenameOnly(data["pwads"], "pwads", warning))
            return JLR_INVALID;
    }

    // playertranslations (validate shape only for now)
    if(data.contains("playertranslations") && !data["playertranslations"].is_null())
    {
        const auto &arr = data["playertranslations"];
        if(!arr.is_array())
        {
            warning(true, "'playertranslations' must be array or null");
            return JLR_INVALID;
        }

        if(arr.size() < 4)
        {
            warning(true, "'playertranslations' must contain at least 4 entries");
            return JLR_INVALID;
        }

        for(const auto &v : arr)
        {
            if(!v.is_string())
            {
                warning(true, "'playertranslations' entries must be strings");
                return JLR_INVALID;
            }
        }
    }

    // If we got here, the data is valid enough to apply to our state, so do that now
    applyDestructiveString(data, "title", g_state.title, g_state.hasTitle);
    applyDestructiveString(data, "author", g_state.author, g_state.hasAuthor);
    applyDestructiveString(data, "description", g_state.description, g_state.hasDescription);
    applyDestructiveString(data, "version", g_state.version, g_state.hasVersion);

    applyDestructiveString(data, "iwad", g_state.iwad, g_state.hasIwad);
    applyDestructiveString(data, "wadtranslation", g_state.wadtranslation, g_state.hasWadTranslation);

    applyPwadsAdditive(data);
    applyOptionsAdditive(data);
    applyPlayerTranslationsDestructive(data);

    return JLR_OK;
}

// Parses the GAMECONF lump by index, returning JLR_OK if it was successfully parsed and processed,
// JLR_INVALID if it was found but had invalid content, or JLR_NO_LUMP if it was not found.
static jsonLumpResult_e parseGameconfLumpByIndex(int lumpnum)
{
    ZAutoBuffer buf;
    wGlobalDir.cacheLumpAuto(lumpnum, buf);
    if(!buf.get() || !buf.getSize())
        return JLR_INVALID;

    return ParseJSONLump(buf.get(), buf.getSize(), "gameconf", { 1, 0, 0 }, parseFunc, nullptr, warningFunc);
}

// Process chain from first-loaded GAMECONF to last-loaded (same approach as DEHACKED queueing).
static bool processGameconfChain(int idx)
{
    if(idx < 0)
        return true;

    lumpinfo_t **lumpinfo = wGlobalDir.getLumpInfo();

    if(!processGameconfChain(lumpinfo[idx]->next))
        return false;

    if(!strncasecmp(lumpinfo[idx]->name, "GAMECONF", 8) && lumpinfo[idx]->li_namespace == lumpinfo_t::ns_global)
    {
        const jsonLumpResult_e r = parseGameconfLumpByIndex(idx);
        if(r != JLR_OK)
        {
            usermsg("GAMECONF rejected (lump #%d).", idx);
            return false;
        }
    }

    return true;
}

// Checks for a GAMECONF lump, and if found, parses it and enables
// ID24 features if it contains "executable" set to "id24".
void ID24_LoadGameconfEarly()
{
    lumpinfo_t *root = wGlobalDir.getLumpNameChain("GAMECONF");
    if(!root)
        return;

    resetGameconfState();

    if(!processGameconfChain(root->index))
    {
        usermsg("GAMECONF pass failed.");
        return;
    }

    if(g_maxExeLevel == GC_EXE_ID24)
        ID24_Enable("gameconf", "executable=id24");

    usermsg("GAMECONF resolved: executable=%s, mode=%s, iwad=%s, pwads=%zu, options=%s", exeToStr(g_maxExeLevel),
            modeToStr(g_maxMode), g_state.hasIwad ? g_state.iwad.c_str() : "unset", g_state.pwads.size(),
            g_state.hasOptions ? "set" : "unset");
}

// Returns whether the GAMECONF specified an IWAD
bool ID24_GameconfHasIwad()
{
    return g_state.hasIwad;
}
// Returns the IWAD filename specified in the GAMECONF, or nullptr if not set
const char *ID24_GameconfIwad()
{
    return g_state.hasIwad ? g_state.iwad.c_str() : nullptr;
}

// Returns the number of PWADs specified in the GAMECONF
size_t ID24_GameconfNumPwads()
{
    return g_state.pwads.size();
}

// Returns the PWAD filename at the specified index in the GAMECONF, or nullptr if the index is out of bounds
const char *ID24_GameconfPwadAt(size_t index)
{
    return index < g_state.pwads.size() ? g_state.pwads[index].c_str() : nullptr;
}

// Returns whether the GAMECONF specified a wad translation
bool ID24_GameconfHasOptions()
{
    return g_state.hasOptions;
}

// Returns the options string specified in the GAMECONF, or nullptr if not set
const char *ID24_GameconfOptions()
{
    return g_state.hasOptions ? g_state.options.c_str() : nullptr;
}

// Returns whether the GAMECONF specified player translations
bool ID24_GameconfHasPlayerTranslations()
{
    return g_state.hasPlayerTranslations;
}

// Returns the number of player translations specified in the GAMECONF
size_t ID24_GameconfNumPlayerTranslations()
{
    return g_state.playertranslations.size();
}

// Returns the player translation filename at the specified index in the GAMECONF, 
// or nullptr if the index is out of bounds
const char *ID24_GameconfPlayerTranslationAt(size_t index)
{
    return index < g_state.playertranslations.size() ? g_state.playertranslations[index].c_str() : nullptr;
}

} // namespace id24
