/*
 * This file is part of the Falcon Player (FPP) and is Copyright (C)
 * 2013-2022 by the Falcon Player Developers.
 *
 * The Falcon Player (FPP) is free software, and is covered under
 * multiple Open Source licenses.  Please see the included 'LICENSES'
 * file for descriptions of what files are covered by each license.
 *
 * This source file is covered under the LGPL v2.1 as described in the
 * included LICENSE.LGPL file.
 */

#include "fpp-pch.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fnmatch.h>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <utility>
#include <vector>

#include "Events.h"
#include "Sequence.h"
#include "common.h"
#include "log.h"
#include "settings.h"
#include "channeloutput/channeloutputthread.h"
#include "commands/Commands.h" // lines 58-58
#include "fseq/FSEQFile.h"

#include "effects.h"

#define MAX_EFFECTS 100

class FPPeffect {
public:
    FPPeffect() :
        fp(nullptr),
        currentFrame(0) {}
    ~FPPeffect() {
        if (fp)
            delete fp;
    }

    std::string name;
    FSEQFile* fp;
    int loop;
    int background;
    uint32_t currentFrame;
};

static int effectCount = 0;
static volatile int pauseBackgroundEffects = 0;
static std::array<FPPeffect*, MAX_EFFECTS> effects;
static std::list<std::pair<uint32_t, uint32_t>> clearRanges;
static std::mutex effectsLock;

/*
 * Initialize effects constructs
 */
int InitEffects(void) {
    std::string localFilename(FPP_DIR_EFFECT("/background.eseq"));

    if ((getFPPmode() == REMOTE_MODE) &&
        CheckForHostSpecificFile(getSetting("HostName").c_str(), localFilename)) {
        localFilename = "background_";
        localFilename += getSetting("HostName");

        LogInfo(VB_EFFECT, "Automatically starting background effect "
                           "sequence %s\n",
                localFilename.c_str());

        StartEffect(localFilename.c_str(), 0, 1, true);
    } else if (FileExists(localFilename)) {
        LogInfo(VB_EFFECT, "Automatically starting background effect sequence "
                           "background.eseq\n");
        StartEffect("background", 0, 1, true);
    }

    pauseBackgroundEffects = getSettingInt("pauseBackgroundEffects");
    registerSettingsListener("effects", "pauseBackgroundEffects",
                             [](const std::string& value) {
                                 LogDebug(VB_SETTING, "Setting pauseBackgroundEffects to %s\n", value.c_str());
                                 pauseBackgroundEffects = getSettingInt("pauseBackgroundEffects");
                             });

    std::function<void(const std::string&, const std::string&)>
        effect_callback = [](const std::string& topic_in,
                             const std::string& payload) {
            LogDebug(VB_CONTROL, "System Callback for %s\n", topic_in.c_str());

            if (0 == topic_in.compare(topic_in.length() - 5, 5, "/stop")) {
                if (payload == "") {
                    StopAllEffects();
                } else {
                    StopEffect(payload.c_str());
                }
            } else if (0 == topic_in.compare(topic_in.length() - 6, 6, "/start")) {
                StartEffect(payload.c_str(), 0);
            } else if (0 == topic_in.compare(topic_in.length() - 8, 8, "/startbg")) {
                StartEffect(payload.c_str(), 0, 0, true);
            }
        };

    Events::AddCallback("/set/effect/#", effect_callback);

    return 1;
}

/*
 * Close effects constructs
 */
void CloseEffects(void) {
}

/*
 * Get next available effect ID
 *
 * Assumes effectsLock is held already
 */
int GetNextEffectID(void) {
    int i = -1;

    for (i = 0; i < MAX_EFFECTS; i++) {
        if (!effects[i])
            return i;
    }

    return -1;
}

/*
 * Check to see if any effects are running
 */
int IsEffectRunning(void) {
    int result = 0;
    std::unique_lock<std::mutex> lock(effectsLock);
    result = effectCount;
    if (!clearRanges.empty()) {
        ++result;
    }
    return result;
}

int StartEffect(FSEQFile* fseq, const std::string& effectName, int loop, bool bg) {
    std::unique_lock<std::mutex> lock(effectsLock);
    if (effectCount >= MAX_EFFECTS) {
        LogErr(VB_EFFECT, "Unable to start effect %s, maximum number of effects already running\n", effectName.c_str());
        return -1;
    }
    int effectID = -1;
    int frameTime = 50;

    frameTime = fseq->getStepTime();
    effectID = GetNextEffectID();

    if (effectID < 0) {
        LogErr(VB_EFFECT, "Unable to start effect %s, unable to determine next effect ID\n", effectName.c_str());
        delete fseq;
        return effectID;
    }

    effects[effectID] = new FPPeffect;
    effects[effectID]->name = effectName;
    effects[effectID]->fp = fseq;
    effects[effectID]->loop = loop;
    effects[effectID]->background = bg;

    effectCount++;
    int tmpec = effectCount;
    lock.unlock();

    StartChannelOutputThread();

    if (!sequence->IsSequenceRunning() && tmpec == 1) {
        // first effect running, no sequence running, set the refresh rate
        // to the rate of the effect
        SetChannelOutputRefreshRate(1000 / frameTime);
    }

    return effectID;
}

int StartFSEQAsEffect(const std::string& fseqName, int loop, bool bg) {
    LogInfo(VB_EFFECT, "Starting FSEQ %s as effect\n", fseqName.c_str());

    std::string filename = FPP_DIR_SEQUENCE("/" + fseqName + ".fseq");

    FSEQFile* fseq = FSEQFile::openFSEQFile(filename);
    if (!fseq) {
        LogErr(VB_EFFECT, "Unable to open effect: %s\n", filename.c_str());
        return -1;
    }
    return StartEffect(fseq, fseqName, loop, bg);
}

/*
 * Start a new effect offset at the specified channel number
 */
int StartEffect(const std::string& effectName, int startChannel, int loop, bool bg) {
    LogInfo(VB_EFFECT, "Starting effect %s at channel %d\n", effectName.c_str(), startChannel);

    std::string filename = FPP_DIR_EFFECT("/" + effectName + ".eseq");

    FSEQFile* fseq = FSEQFile::openFSEQFile(filename);
    if (!fseq) {
        LogErr(VB_EFFECT, "Unable to open effect: %s\n", filename.c_str());
        return -1;
    }
    V2FSEQFile* v2fseq = dynamic_cast<V2FSEQFile*>(fseq);
    if (!v2fseq) {
        delete fseq;
        LogErr(VB_EFFECT, "Effect file not a correct eseq file: %s\n", filename.c_str());
        return -1;
    }

    if (v2fseq->m_sparseRanges.size() == 0) {
        LogErr(VB_EFFECT, "eseq file must have at least one model range.");
        delete fseq;
        return -1;
    }

    if (startChannel != 0) {
        // This will need to change if/when we support multiple models per file
        v2fseq->m_sparseRanges[0].first = startChannel - 1;
    }
    return StartEffect(v2fseq, effectName, loop, bg);
}

/*
 * Helper function to stop an effect, assumes effectsLock is already held
 */
void StopEffectHelper(int effectID) {
    FPPeffect* e = NULL;
    e = effects[effectID];

    if (e->fp) {
        V2FSEQFile* v2fseq = dynamic_cast<V2FSEQFile*>(e->fp);
        if (v2fseq && v2fseq->m_sparseRanges.size() != 0) {
            for (auto& a : v2fseq->m_sparseRanges) {
                clearRanges.push_back(std::pair<uint32_t, uint32_t>(a.first, a.second));
            }
            for (auto& a : v2fseq->m_rangesToRead) {
                clearRanges.push_back(std::pair<uint32_t, uint32_t>(a.first, a.second));
            }
        } else {
            // not sparse and not eseq, entire range
            clearRanges.push_back(std::pair<uint32_t, uint32_t>(0, e->fp->getChannelCount()));
        }
    }
    delete e;
    effects[effectID] = NULL;
    effectCount--;
}

/*
 * Stop all effects named effectName
 */
int StopEffect(const std::string& effectName) {
    LogDebug(VB_EFFECT, "StopEffect(%s)\n", effectName.c_str());

    std::unique_lock<std::mutex> lock(effectsLock);
    std::vector<std::string> names = split(effectName, ',');

    for (int j = 0; j < names.size(); j++) {
        for (int i = 0; i < MAX_EFFECTS; i++) {
            if (effects[i]) {
                if ((effects[i]->name == names[j]) ||
                    (!fnmatch(names[j].c_str(), effects[i]->name.c_str(), FNM_PATHNAME))) {
                    StopEffectHelper(i);
                }
            }
        }
    }

    lock.unlock();

    if ((!IsEffectRunning()) &&
        (!sequence->IsSequenceRunning())) {
        sequence->SendBlankingData();
    }

    return 1;
}

/*
 * Stop a single effect
 */
int StopEffect(int effectID) {
    FPPeffect* e = NULL;

    LogDebug(VB_EFFECT, "StopEffect(%d)\n", effectID);

    std::unique_lock<std::mutex> lock(effectsLock);
    if (!effects[effectID]) {
        return 0;
    }

    StopEffectHelper(effectID);
    lock.unlock();

    if ((!IsEffectRunning()) &&
        (!sequence->IsSequenceRunning())) {
        sequence->SendBlankingData();
    }

    return 1;
}

/*
 * Stop all effects
 */
void StopAllEffects(void) {
    int i;

    LogDebug(VB_EFFECT, "Stopping all effects\n");

    std::unique_lock<std::mutex> lock(effectsLock);

    for (i = 0; i < MAX_EFFECTS; i++) {
        if (effects[i])
            StopEffectHelper(i);
    }
    lock.unlock();
    if ((!IsEffectRunning()) &&
        (!sequence->IsSequenceRunning()))
        sequence->SendBlankingData();
}

/*
 * Overlay a single effect onto raw channel data
 */
int OverlayEffect(int effectID, char* channelData) {
    FPPeffect* e = NULL;
    if (!effects[effectID]) {
        LogErr(VB_EFFECT, "Invalid Effect ID %d\n", effectID);
        return 0;
    }

    e = effects[effectID];
    FSEQFile::FrameData* d = e->fp->getFrame(e->currentFrame);
    if (d == nullptr && e->loop) {
        e->currentFrame = 0;
        d = e->fp->getFrame(e->currentFrame);
    }
    e->currentFrame++;
    if (d) {
        d->readFrame((uint8_t*)channelData, FPPD_MAX_CHANNELS);
        delete d;
        return 1;
    } else {
        StopEffectHelper(effectID);
        for (auto& rng : clearRanges) {
            memset(&channelData[rng.first], 0, rng.second);
        }
        clearRanges.clear();
    }

    return 0;
}

/*
 * Overlay current effects onto raw channel data
 */
int OverlayEffects(char* channelData) {
    int i;
    int dataRead = 0;

    std::unique_lock<std::mutex> lock(effectsLock);

    // for effects that have been stopped, we need to clear the data
    for (auto& rng : clearRanges) {
        memset(&channelData[rng.first], 0, rng.second);
    }
    clearRanges.clear();

    if (effectCount == 0) {
        return 0;
    }

    int skipBackground = 0;
    if (pauseBackgroundEffects && sequence->IsSequenceRunning()) {
        skipBackground = 1;
    }

    for (i = 0; i < MAX_EFFECTS; i++) {
        if (effects[i]) {
            if ((!skipBackground) ||
                (skipBackground && (!effects[i]->background))) {
                dataRead |= OverlayEffect(i, channelData);
            }
        }
    }

    lock.unlock();

    if ((dataRead == 0) &&
        (!IsEffectRunning()) &&
        (!sequence->IsSequenceRunning())) {
        sequence->SendBlankingData();
    }

    return 1;
}

Json::Value GetRunningEffectsJson() {
    Json::Value arr(Json::arrayValue);
    int i;
    std::unique_lock<std::mutex> lock(effectsLock);

    for (i = 0; i < MAX_EFFECTS; i++) {
        if (effects[i]) {
            Json::Value obj;
            obj["id"] = i;
            obj["name"] = effects[i]->name;
            arr.append(obj);
        }
    }

    return arr;
}

/*
 * Get list of running effects and their IDs
 *
 * Format: [EFFECTID1,EFFECTNAME1[,EFFECTID2,EFFECTNAME2]...]
 *
 * NOTE: Caller is responsible for freeing string allocated
 */
int GetRunningEffects(char* msg, char** result) {
    int length = strlen(msg) + 2; // 1 for LF, 1 for NULL termination
    int i = 0;

    std::unique_lock<std::mutex> lock(effectsLock);

    for (i = 0; i < MAX_EFFECTS; i++) {
        if (effects[i]) {
            // delimiters
            length += 2;

            // ID
            length++;
            if (i > 9)
                length++;
            if (i > 99)
                length++;

            // Name
            length += strlen(effects[i]->name.c_str());
        }
    }

    *result = (char*)malloc(length);
    char* cptr = *result;
    *cptr = '\0';

    strcat(cptr, msg);
    cptr += strlen(msg);

    for (i = 0; i < MAX_EFFECTS; i++) {
        if (effects[i]) {
            strcat(cptr, ";");
            cptr++;

            cptr += snprintf(cptr, 4, "%d", i);

            strcat(cptr, ",");
            cptr++;

            strcat(cptr, effects[i]->name.c_str());
            cptr += strlen(effects[i]->name.c_str());
        }
    }

    strcat(cptr, "\n");
    return strlen(*result);
}
