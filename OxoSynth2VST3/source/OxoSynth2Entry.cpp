#include "OxoSynth2CIDs.h"
#include "OxoSynth2Controller.h"
#include "OxoSynth2Processor.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#define stringPluginName OXO_PRODUCT_NAME
#define stringVersion OXO_VERSION

using namespace Steinberg;
using namespace Steinberg::Vst;

BEGIN_FACTORY_DEF(OXO_VENDOR, OXO_VENDOR_URL, OXO_VENDOR_EMAIL)

DEF_CLASS2(INLINE_UID_FROM_FUID(oxo::kOxoSynth2ProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
           stringPluginName, Vst::kDistributable, Vst::PlugType::kInstrumentSynth, stringVersion, kVstVersionString,
           oxo::OxoSynth2Processor::createInstance)

DEF_CLASS2(INLINE_UID_FROM_FUID(oxo::kOxoSynth2ControllerUID), PClassInfo::kManyInstances,
           kVstComponentControllerClass, stringPluginName "Controller", 0, "", stringVersion, kVstVersionString,
           oxo::OxoSynth2Controller::createInstance)

END_FACTORY
