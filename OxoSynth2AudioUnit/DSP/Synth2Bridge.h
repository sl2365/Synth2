#pragma once

// C API over Synth2Instance for the Swift AUAudioUnit (no C++ in Swift).

#include <AudioToolbox/AudioToolbox.h>
#include <stdint.h>

#include "../../EditorCore/OxoSynth2Bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*Synth2EditCallback)(void *ctx, int32_t id, int32_t value, int32_t phase);

// factoryRoot = the read-only factory bank folder (appex resources), may be NULL.
void *Synth2Create(const char *libraryRoot, const char *factoryRoot, const char *build);
void Synth2Destroy(void *instance);
void Synth2SetEditCallback(void *instance, void *ctx, Synth2EditCallback callback);
OxoSynth2Hooks Synth2MakeHooks(void *instance);

int32_t Synth2ParamCount(void); // host parameters: the 99 of the patch + the master volume (ID 99)
const char *Synth2ParamKey(int32_t id);
const char *Synth2ParamName(int32_t id);
void Synth2ParamRange(int32_t id, int32_t *min, int32_t *max, int32_t *def);
int32_t Synth2ParamAutomatable(int32_t id);
/// Writes the display text of `value` into `out` (NUL-terminated); LFO
/// speeds read as note divisions while the instance's tempo sync is on.
void Synth2FormatParam(void *instance, int32_t id, int32_t value, char *out, int32_t capacity);

int32_t Synth2GetParam(void *instance, int32_t id);
void Synth2SetParam(void *instance, int32_t id, int32_t value);

/// State chunk ('OXS2' + version + .sy1 text). Returns the size; copies up to
/// `capacity` bytes into `out` (call with out = NULL to query).
int32_t Synth2EncodeState(void *instance, uint8_t *out, int32_t capacity);
int32_t Synth2RestoreState(void *instance, const uint8_t *data, int32_t size);

void Synth2Prepare(void *instance, double sampleRate, uint32_t maxFrames);
void Synth2Render(void *instance, uint32_t frames, AudioBufferList *output, const AURenderEvent *events,
                  int64_t now, double tempo, double beat, int32_t playing);

#ifdef __cplusplus
}
#endif
