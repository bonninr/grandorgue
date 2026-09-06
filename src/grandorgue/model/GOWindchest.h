/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOWINDCHEST_H
#define GOWINDCHEST_H

#include <atomic>
#include <vector>

#include <wx/string.h>

#include "pipe-config/GOPipeConfigTreeNode.h"

#include "GOOrganLifecycleListener.h"

class GOConfigReader;
class GOEnclosure;
class GOPipeWindchestCallback;
class GORank;
class GOTremulant;
class GOOrganModel;

class GOWindchest : private GOOrganLifecycleListener {
private:
  GOOrganModel &r_OrganModel;
  wxString m_Name;
  // a name tat is never translated. May be empty that causes that m_name is
  // used
  wxString m_HardName;

  float m_Volume;

  /* Wind supply, as Hauptwerk describes it. m_WindCapacity is the reservoir
   * this chest draws on, in the same units as the per-pipe demand, and zero
   * means an unlimited supply - the ordinary case, and what every organ
   * without a wind model behaves like. m_WindDemand is written from the
   * thread that presses keys and read from the audio threads, hence atomic. */
  float m_WindCapacity;
  std::atomic<float> m_WindDemand;
  std::vector<GOEnclosure *> m_enclosure;
  std::vector<unsigned> m_tremulant;
  std::vector<GORank *> m_ranks;
  std::vector<GOPipeWindchestCallback *> m_pipes;
  GOPipeConfigTreeNode m_PipeConfig;

  void PreparePlayback();

public:
  GOWindchest(GOOrganModel &organModel);

  const wxString &GetHardName() { return m_HardName; }
  void SetHardName(const wxString &name) { m_HardName = name; }

  void Init(GOConfigReader &cfg, wxString group, wxString name);
  void Load(GOConfigReader &cfg, wxString group, unsigned index);
  void UpdateTremulant(GOTremulant *tremulant);
  void UpdateVolume();
  float GetVolume();

  /** Whether this chest models a limited wind supply at all. */
  bool HasWindModel() const { return m_WindCapacity > 0; }
  /**
   * Called as pipes start and stop speaking, with the air a pipe draws.
   * @param flow positive when the pipe starts, negative when it stops
   */
  void AddWindDemand(float flow);
  /**
   * @return the factor the chest's output is scaled by at the current demand:
   *   1 when the supply keeps up, falling towards a floor as it stops doing
   *   so. Cheap enough to call once per audio block per chest.
   */
  float GetWindPressureFactor() const;
  /**
   * @return what to multiply a pipe's playback rate by at the current
   *   pressure. A pipe starved of wind goes flat, which is most of what makes
   *   wind sag recognisable as wind rather than as a volume dip.
   */
  float GetWindPitchFactor() const;
  unsigned GetTremulantCount();
  unsigned GetTremulantId(unsigned index);
  unsigned GetRankCount();
  GORank *GetRank(unsigned index);
  void AddRank(GORank *rank);
  void AddPipe(GOPipeWindchestCallback *pipe);
  void AddEnclosure(GOEnclosure *enclosure);
  const std::vector<GOEnclosure *> &GetEnclosures() const {
    return m_enclosure;
  }
  const wxString &GetName();
  GOPipeConfigNode &GetPipeConfig();
};

#endif /* GOWINDCHEST_H_ */
