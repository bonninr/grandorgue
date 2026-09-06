/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOSoundWindchestTask.h"

#include "sound/GOSoundOrganEngine.h"
#include "threading/GOMutexLocker.h"

#include "GOSoundTremulantTask.h"

GOSoundWindchestTask::GOSoundWindchestTask(
  GOSoundOrganEngine &soundEngine, GOWindchest *pWindchest)
  : r_engine(soundEngine),
    m_volume(0),
    m_pitchFactor(1.0f),
    m_IsPitchMoving(false),
    m_done(false),
    p_windchest(pWindchest) {}

void GOSoundWindchestTask::Init(
  ptr_vector<GOSoundTremulantTask> &tremulantTasks) {
  m_pTremulantTasks.clear();
  if (p_windchest)
    for (unsigned i = 0; i < p_windchest->GetTremulantCount(); i++)
      m_pTremulantTasks.push_back(
        tremulantTasks[p_windchest->GetTremulantId(i)]);

  /* Whether the pitch can move on this chest at all, decided once: a chest
   * with a steady wind, whose tremulants only change the loudness, never
   * reaches the retuning. */
  m_IsPitchMoving = p_windchest && p_windchest->HasWindModel();
  for (GOSoundTremulantTask *pTask : m_pTremulantTasks)
    if (pTask->IsPitchMoving())
      m_IsPitchMoving = true;
}

void GOSoundWindchestTask::NewRound() {
  GOMutexLocker locker(m_mutex);

  m_done.store(false);
}

void GOSoundWindchestTask::Run(GOSchedulerThread *pThread) {
  if (!m_done.load()) {
    GOMutexLocker locker(m_mutex);

    if (!m_done.load()) {
      float volume = r_engine.GetGain();
      float pitchFactor = 1.0f;

      if (p_windchest) {
        volume *= p_windchest->GetVolume();
        // The wind model is one more multiplier on the same signal the
        // tremulants already modulate, and costs nothing on a chest that
        // declares no supply limit.
        volume *= p_windchest->GetWindPressureFactor();
        pitchFactor = p_windchest->GetWindPitchFactor();
        for (unsigned i = 0; i < m_pTremulantTasks.size(); i++) {
          volume *= m_pTremulantTasks[i]->GetVolume();
          // The same signal, applied to the rate instead of the level
          pitchFactor *= m_pTremulantTasks[i]->GetPitchFactor();
        }
      }
      m_volume = volume;
      m_pitchFactor = pitchFactor;
      m_done.store(true);
    }
  }
}
