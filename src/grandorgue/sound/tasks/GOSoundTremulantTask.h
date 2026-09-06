/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOSOUNDTREMULANTTASK_H
#define GOSOUNDTREMULANTTASK_H

#include "sound/playing/GOSoundSamplerList.h"
#include "threading/GOMutex.h"

#include "GOSoundTaskBase.h"

class GOSchedulerThread;
class GOSoundSamplerPlayer;

class GOSoundTremulantTask : public GOSoundTaskBase {
private:
  GOSoundSamplerPlayer &r_SamplerPlayer;
  GOSoundSamplerList m_Samplers;
  GOMutex m_Mutex;
  float m_Volume;
  // A fraction of the level, ready to multiply
  float m_AmpModDepth;
  // Cents either way, turned into a rate multiplier when it is applied
  float m_PitchModDepthCents;
  unsigned m_SamplesPerBuffer;
  bool m_Done;

public:
  GOSoundTremulantTask(
    GOSoundSamplerPlayer &samplerPlayer, unsigned nFramesPerBuffer);

  unsigned GetPriority() const override { return PRIORITY_TREMULANT; }
  unsigned GetCost() const override { return 0; }
  bool IsRepeatable() const override { return false; }
  void Run(GOSchedulerThread *pThread = nullptr) override;
  void CompleteRound() override { Run(); }

  void NewRound() override;
  void DiscardContent() override { m_Samplers.Clear(); }
  void Add(GOSoundSampler *sampler);

  float GetVolume() {
    if (!m_Done)
      Run();
    return m_Volume;
  }

  /**
   * States how far this tremulant swings, so the pitch can follow the
   * loudness.
   *
   * @param ampModDepth the loudness swing, as a percentage of the level
   * @param pitchModDepthCents the pitch swing, in cents either way
   */
  void SetModDepths(unsigned ampModDepth, unsigned pitchModDepthCents);

  /**
   * @return what to multiply a pipe's playback rate by this block, or 1 when
   *   the tremulant does not move the pitch
   */
  float GetPitchFactor();

  /** @return whether this tremulant moves the pitch at all */
  bool IsPitchMoving() const { return m_PitchModDepthCents > 0.0f; }
};

#endif
