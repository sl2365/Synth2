#include "TestFramework.hpp"

#include "../EngineCore/MidiLearn.hpp"

using namespace oxo::s2;

OXO_TEST(MidiLearnScalesOverFullRange) {
    OXO_EXPECT_EQ(ScaledValueForCC(kFilterFreq, 0), 0);
    OXO_EXPECT_EQ(ScaledValueForCC(kFilterFreq, 127), 127);
    OXO_EXPECT_EQ(ScaledValueForCC(kFilterFreq, 64), 64);
    OXO_EXPECT_EQ(ScaledValueForCC(kOscKeyShift, 0), -24);
    OXO_EXPECT_EQ(ScaledValueForCC(kOscKeyShift, 127), 24);
    OXO_EXPECT_EQ(ScaledValueForCC(kArpOn, 63), 0); // switches flip at 64
    OXO_EXPECT_EQ(ScaledValueForCC(kArpOn, 64), 1);
    OXO_EXPECT_EQ(ScaledValueForCC(kLfo1Dest, 0), 1);
    OXO_EXPECT_EQ(ScaledValueForCC(kLfo1Dest, 127), 7);
}

OXO_TEST(MidiLearnHiddenWordsNotLearnable) {
    OXO_EXPECT(!IsLearnable(kCtrlSrc1));
    OXO_EXPECT(!IsLearnable(kCtrlAssign2));
    OXO_EXPECT(IsLearnable(kPolyphony));
    OXO_EXPECT(IsLearnable(kFilterFreq));
}

OXO_TEST(MidiLearnRoundTrip) {
    MidiLearnTable t{};
    t[kFilterFreq] = 74;
    t[kFilterRes] = 71;
    t[kAmpRelease] = 74; // one CC, several params
    t[kCtrlSrc1] = 5;    // not learnable: dropped
    const MidiLearnTable back = DecodeMidiLearn(EncodeMidiLearn(t));
    OXO_EXPECT_EQ(back[kFilterFreq], 74);
    OXO_EXPECT_EQ(back[kFilterRes], 71);
    OXO_EXPECT_EQ(back[kAmpRelease], 74);
    OXO_EXPECT_EQ(back[kCtrlSrc1], 0);
    OXO_EXPECT_EQ(DecodeMidiLearn(EncodeMidiLearn(MidiLearnTable{}))[kFilterFreq], 0);
    OXO_EXPECT_EQ(DecodeMidiLearn("{\"bogus\": 3, \"filter_freq\": 300, \"pan\":10}")[kPan], 10);
    OXO_EXPECT_EQ(DecodeMidiLearn("{\"filter_freq\": 300}")[kFilterFreq], 0);
}
