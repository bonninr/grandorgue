/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOSOUNDWINDCHESTTASK_H
#define GOSOUNDWINDCHESTTASK_H

#include <atomic>

#include "model/GOWindchest.h"
#include "threading/GOMutex.h"

#include "GOSoundTaskBase.h"
#include "ptrvector.h"

class GOSchedulerThread;
class GOSoundOrganEngine;
class GOSoundTremulantTask;
class GOWindchest;

class GOSoundWindchestTask : public GOSoundTaskBase {
private:
  GOSoundOrganEngine &r_engine;
  GOMutex m_mutex;
  float m_volume;
  // What the wind and the tremulants together do to the pitch
  float m_pitchFactor;
  // Whether anything moves it at all, so a still chest pays nothing
  bool m_IsPitchMoving;
  std::atomic_bool m_done;
  GOWindchest *p_windchest;
  std::vector<GOSoundTremulantTask *> m_pTremulantTasks;

public:
  GOSoundWindchestTask(
    GOSoundOrganEngine &sound_engine, GOWindchest *windchest);

  unsigned GetPriority() const override { return PRIORITY_WINDCHEST; }
  unsigned GetCost() const override { return 0; }
  bool IsRepeatable() const override { return false; }
  void Run(GOSchedulerThread *pThread = nullptr) override;
  void CompleteRound() override {}

  void DiscardContent() override { NewRound(); }
  void NewRound() override;
  void Init(ptr_vector<GOSoundTremulantTask> &tremulantTasks);

  /** @return the chest this task belongs to, or nullptr for the special one. */
  bool IsPitchMoving() const { return m_IsPitchMoving; }

  /**
   * @return what to multiply a pipe's playback rate by this block
   */
  float GetPitchFactor() {
    Run();
    return m_pitchFactor;
  }

  const GOWindchest *GetWindchest() const { return p_windchest; }

  float GetWindchestVolume() const {
    return p_windchest ? p_windchest->GetVolume() : 1;
  }

  float GetVolume() {
    if (!m_done.load())
      Run();
    return m_volume;
  }
};

#endif
