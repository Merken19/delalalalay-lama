// Profile keys of DelayLamaVoice's articulator: DL_KEY(id, name, low, high,
// default, nullable). Delay Lama Everything docs/IMITATION.md section 7 is the
// spec; nullable keys take -1 for null. Included by voice_articulator.h/.cpp.

DL_KEY(RangeOctave, "range.octave", -3, 3, 0, false)
DL_KEY(RangeFit, "range.fit", 0, 1, 0, false)
DL_KEY(RangeTarget, "range.target", 0, 127, 57, false)
DL_KEY(RangeLow, "range.low", 0, 127, 36, false)
DL_KEY(RangeHigh, "range.high", 0, 127, 72, false)
DL_KEY(RangeFold, "range.fold", 0, 1, 0, false)

DL_KEY(OnsetVowel, "onset.vowel", 0, 1, -1, true)
DL_KEY(OnsetTime, "onset.time", 0, 300, 0, false)
DL_KEY(OnsetCurve, "onset.curve", 0, 4, 3, false)
DL_KEY(OnsetBurst, "onset.burst", 0, 1, -1, true)
DL_KEY(OnsetBurstTime, "onset.burst_time", 0, 150, 0, false)
DL_KEY(OnsetScoop, "onset.scoop", -12, 12, 0, false)
DL_KEY(OnsetScoopTime, "onset.scoop_time", 0, 300, 0, false)
DL_KEY(OnsetScoopCurve, "onset.scoop_curve", 0, 4, 2, false)
DL_KEY(OnsetAttack, "onset.attack", 0, 2000, 0, false)
DL_KEY(OnsetPeak, "onset.peak", 0, 1.5, 1, false)
DL_KEY(OnsetLips, "onset.lips", 0, 1, 0, false)
// Phase 4b addition: when the onset vowel differs from the nucleus, the attack
// lasts at least mask x onset.time, so the onset colour is never heard at
// full level. 0 = off, the default since the user's ear check (it removed
// none of the "laser" onsets; D26).
DL_KEY(OnsetMask, "onset.mask", 0, 3, 0, false)
// Phase 4b round 3: formant bandwidth factor while the onset vowel moves
// (the DSP's formants are narrow, so a moving vowel rings as a pitch); held
// through the move, then back to 1 over the same time. 1 = off (D33).
DL_KEY(OnsetBandwidth, "onset.bandwidth", 1, 8, 1, false)
// Phase 4b round 3: the onset vowel move only on a note that starts after
// the channel was silent at least this long (ms); other notes start steady.
// 0 = every new or re-articulated note (D33).
DL_KEY(OnsetAfterRest, "onset.after_rest", 0, 5000, 0, false)

DL_KEY(NucleusVowel, "nucleus.vowel", 0, 1, 0.5, false)
DL_KEY(NucleusPitchTrack, "nucleus.pitch_track", -0.3, 0.3, 0.05, false)
DL_KEY(NucleusVelocityTrack, "nucleus.velocity_track", -0.3, 0.3, 0, false)

DL_KEY(BodyBreath, "body.breath", 0, 1, 0.75, false)
DL_KEY(BodyDecay, "body.decay", 0, 10000, 0, false)
DL_KEY(BodySustain, "body.sustain", 0, 1, 1, false)
DL_KEY(BodyDecayTrack, "body.decay_track", 0.5, 1.5, 1, false)
DL_KEY(BodyVowel, "body.vowel", 0, 1, -1, true)
DL_KEY(BodyVowelTime, "body.vowel_time", 0, 5000, 0, false)
DL_KEY(BodyMax, "body.max", 0, 5000, 0, false)
DL_KEY(BodyTremolo, "body.tremolo", 0, 1, 0, false)
DL_KEY(BodyTremoloRate, "body.tremolo_rate", 0.5, 30, 6, false)
DL_KEY(BodyEndDb, "body.end_db", -60, -20, -40, false)

DL_KEY(EndingRelease, "ending.release", 5, 5000, 20, false)
DL_KEY(EndingVowel, "ending.vowel", 0, 1, -1, true)
DL_KEY(EndingVowelTime, "ending.vowel_time", 0, 2000, -1, true)
DL_KEY(EndingFall, "ending.fall", -12, 12, 0, false)
DL_KEY(EndingFallTime, "ending.fall_time", 0, 1000, 0, false)
DL_KEY(EndingBreath, "ending.breath", 0, 1, -1, true)
DL_KEY(EndingLips, "ending.lips", 0, 1, 0, false)

DL_KEY(PhraseMode, "phrase.mode", 0, 3, 0, false)
DL_KEY(PhraseGap, "phrase.gap", 0, 200, 30, false)
DL_KEY(PhraseOverlap, "phrase.overlap", 0, 200, 10, false)
DL_KEY(PhraseGlideMode, "phrase.glide_mode", 0, 1, 0, false)
DL_KEY(PhraseGlide, "phrase.glide", 0.002, 1, 0.5, false)
DL_KEY(PhraseGlideTime, "phrase.glide_time", 0, 500, 60, false)
DL_KEY(PhraseGlideCurve, "phrase.glide_curve", 0, 4, 3, false)
DL_KEY(PhraseLegatoVowelTime, "phrase.legato_vowel_time", 0, 500, 80, false)
DL_KEY(PhraseDipTime, "phrase.dip_time", 0, 80, 25, false)
DL_KEY(PhraseDipLevel, "phrase.dip_level", 0, 1, 0.2, false)
DL_KEY(PhraseDipVowel, "phrase.dip_vowel", 0, 1, -1, true)
DL_KEY(PhraseCompress, "phrase.compress", 0, 1, 1, false)

DL_KEY(VibratoDepth, "vibrato.depth", 0.2, 1.2, 0.2, false)
DL_KEY(VibratoRate, "vibrato.rate", 1.5, 24, 6, false)
DL_KEY(VibratoDelay, "vibrato.delay", 0, 2000, 0, false)
DL_KEY(VibratoFade, "vibrato.fade", 0, 2000, 0, false)
DL_KEY(VibratoModwheel, "vibrato.modwheel", 0, 1, 0.3, false)

DL_KEY(VoiceSize, "voice.size", 0, 1, 0.5, false)
DL_KEY(VoicePitchTrack, "voice.pitch_track", -0.2, 0.2, 0, false)

DL_KEY(EnsembleUnison, "ensemble.unison", 1, 10, 1, false)
DL_KEY(EnsembleDetune, "ensemble.detune", 0, 50, 0, false)
DL_KEY(EnsembleSpread, "ensemble.spread", 0, 1, 0, false)

DL_KEY(DynVelocity, "dyn.velocity", 0, 2, 1, false)
DL_KEY(DynBurstVelocity, "dyn.burst_velocity", 0, 1, 0.5, false)
// Accepted for the 4c tuning; not acted on yet.
DL_KEY(DynEven, "dyn.even", 0, 1, 0, false)
