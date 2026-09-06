/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOSOUNDPROVIDER_H_
#define GOSOUNDPROVIDER_H_

#include <cstdint>
#include <vector>

#include "sound/playing/GOSoundFilter.h"
#include "sound/playing/GOSoundToneBalanceFilter.h"

#include "GOBool3.h"
#include "GOStatisticCallback.h"
#include "ptrvector.h"

class GOSoundAudioSection;
class GOCache;
class GOCacheWriter;
class GOHash;
class GOMemoryPool;

typedef struct audio_section_stream_s audio_section_stream;

class GOSoundProvider : public GOStatisticCallback {
protected:
  struct AttackSelector {
    unsigned min_attack_velocity;
    unsigned max_released_time;
    GOBool3 m_WaveTremulantStateFor;
  };

  struct ReleaseSelector {
    unsigned max_playback_time;
    GOBool3 m_WaveTremulantStateFor;
  };

  unsigned m_MidiKeyNumber;
  float m_MidiPitchFract;
  float m_Gain;
  float m_Tuning;
  int8_t m_ToneBalanceValue;
  GOSoundToneBalanceFilter m_ToneBalance;
  /* A shelf above a stated frequency, which is how a sample set voices a
   * pipe brighter or duller than it was recorded. Separate from the tone
   * balance because that one belongs to the listener and this one to the
   * organ; both apply. */
  GOSoundFilter m_VoicingFilter;
  double m_VoicingFilterFrequency;
  double m_VoicingFilterGain;
  bool m_IsWaveTremulantActive;
  unsigned m_ReleaseTail;
  ptr_vector<GOSoundAudioSection> m_Attack;
  std::vector<AttackSelector> m_AttackInfo;
  ptr_vector<GOSoundAudioSection> m_Release;
  std::vector<ReleaseSelector> m_ReleaseInfo;
  void ComputeReleaseAlignmentInfo();
  float m_VelocityVolumeBase;
  float m_VelocityVolumeIncrement;
  unsigned m_AttackSwitchCrossfadeLength;

public:
  static void UpdateCacheHash(GOHash &hash);

  GOSoundProvider();
  virtual ~GOSoundProvider();

  void ClearData();

  virtual bool LoadCache(GOMemoryPool &pool, GOCache &cache);
  virtual bool SaveCache(GOCacheWriter &cache) const;

  bool IsWaveTremulant() const { return m_IsWaveTremulantActive; }
  void SetWaveTremulant(bool isActive) { m_IsWaveTremulantActive = isActive; }

  void SetVelocityParameter(float min_volume, float max_volume);

  bool IsWaveTremulantStateSuitable(GOBool3 waveTremulantStateFor) const {
    return to_bool(waveTremulantStateFor, m_IsWaveTremulantActive)
      == m_IsWaveTremulantActive;
  }

  const GOSoundAudioSection *GetAttack(
    unsigned velocity, unsigned releasedDurationMs) const;
  const GOSoundAudioSection *GetRelease(
    GOBool3 waveTremulantStateFor, unsigned playbackDurationMs) const;
  float GetGain() const;
  int IsOneshot() const;

  float GetTuning() const;
  void SetTuning(float cent);
  int8_t GetToneBalanceValue() const;
  void SetToneBalanceValue(int8_t value);
  void SetToneBalanceFilterSamplerate(unsigned samplerate);
  const GOSoundToneBalanceFilter *GetToneBalance() const {
    return &m_ToneBalance;
  }

  /**
   * Voices this pipe with a shelf above a frequency.
   *
   * @param frequency where the shelf starts, in Hz; zero switches it off
   * @param gainDecibels how much to lift or drop everything above it
   */
  void SetVoicingFilter(double frequency, double gainDecibels);
  const GOSoundFilter *GetVoicingFilter() const { return &m_VoicingFilter; }
  unsigned GetReleaseTail() const { return m_ReleaseTail; }
  void SetReleaseTail(unsigned releaseTail) { m_ReleaseTail = releaseTail; }

  unsigned GetMidiKeyNumber() const;
  float GetMidiPitchFract() const;
  unsigned GetAttackSwitchCrossfadeLength() const {
    return m_AttackSwitchCrossfadeLength;
  }

  float GetVelocityVolume(unsigned velocity) const;

  bool CheckForMissingAttack();
  bool CheckForMissingRelease();
  bool CheckMissingRelease();
  bool CheckNotNecessaryRelease();

  GOSampleStatistic GetStatistic();
};

inline float GOSoundProvider::GetGain() const { return m_Gain; }

inline float GOSoundProvider::GetTuning() const { return m_Tuning; }

inline int8_t GOSoundProvider::GetToneBalanceValue() const {
  return m_ToneBalanceValue;
}

#endif /* GOSOUNDPROVIDER_H_ */
