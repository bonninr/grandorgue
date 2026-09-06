/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOHauptwerkToOdf.h"

#include <wx/filename.h>
#include <wx/intl.h>

#include <algorithm>

#include "GOHauptwerkOdf.h"

// Hauptwerk object types
static const wxString WX_GENERAL = wxT("_General");
static const wxString WX_DIVISION = wxT("Division");
static const wxString WX_KEYBOARD = wxT("Keyboard");
static const wxString WX_STOP = wxT("Stop");
static const wxString WX_STOP_RANK = wxT("StopRank");
static const wxString WX_RANK = wxT("Rank");
static const wxString WX_PIPE = wxT("Pipe_SoundEngine01");
static const wxString WX_LAYER = wxT("Pipe_SoundEngine01_Layer");
static const wxString WX_ATTACK = wxT("Pipe_SoundEngine01_AttackSample");
static const wxString WX_RELEASE = wxT("Pipe_SoundEngine01_ReleaseSample");
static const wxString WX_SAMPLE = wxT("Sample");
static const wxString WX_WIND_COMPARTMENT = wxT("WindCompartment");
static const wxString WX_TREMULANT = wxT("Tremulant");
static const wxString WX_ENCLOSURE = wxT("Enclosure");
static const wxString WX_ENCLOSURE_PIPE = wxT("EnclosurePipe");
static const wxString WX_KEY_ACTION = wxT("KeyAction");
static const wxString WX_DIVISION_INPUT = wxT("DivisionInput");

// Hauptwerk attribute names used in more than one place
static const wxString WX_NAME = wxT("Name");
static const wxString WX_RANK_ID = wxT("RankID");
static const wxString WX_PIPE_ID = wxT("PipeID");
static const wxString WX_LAYER_ID = wxT("LayerID");
static const wxString WX_SAMPLE_ID = wxT("SampleID");
static const wxString WX_STOP_ID = wxT("StopID");
static const wxString WX_DIVISION_ID = wxT("DivisionID");

// Hauptwerk marks the release that catches every remaining key-press length
// with this instead of a real limit.
static const long HW_UNLIMITED_KEY_PRESS_MS = 99999;

static const wxString WX_ORGAN = wxT("Organ");
static const wxString WX_ODF_YES = wxT("Y");
static const wxString WX_ODF_NO = wxT("N");

/** Group name with the three-digit suffix GrandOrgue ODFs use. */
static wxString numbered(const wxString &prefix, unsigned n) {
  return wxString::Format(wxT("%s%03u"), prefix, n);
}

void GOHauptwerkToOdf::FillReadFilter(
  std::map<wxString, std::set<wxString>> &outFilter) {
  // An empty set keeps every attribute of that type. The bulk types are
  // listed explicitly: Sample alone runs to 60,000 objects, and holding only
  // the three fields that matter keeps the read affordable on a small machine.
  outFilter[WX_GENERAL] = {};
  outFilter[WX_DIVISION] = {};
  outFilter[WX_KEYBOARD] = {};
  outFilter[WX_STOP] = {};
  outFilter[WX_STOP_RANK] = {};
  outFilter[WX_RANK] = {};
  outFilter[WX_WIND_COMPARTMENT] = {};
  outFilter[WX_TREMULANT] = {};
  outFilter[WX_ENCLOSURE] = {};
  outFilter[WX_ENCLOSURE_PIPE] = {WX_PIPE_ID, wxT("EnclosureID")};
  outFilter[WX_KEY_ACTION] = {};
  outFilter[WX_DIVISION_INPUT]
    = {WX_DIVISION_ID, wxT("NormalMIDINoteNumber")};
  outFilter[WX_PIPE] = {
    WX_PIPE_ID,
    WX_RANK_ID,
    wxT("NormalMIDINoteNumber"),
    wxT("Pitch_Tempered_RankBasePitch64ftHarmonicNum"),
    wxT("Pitch_OriginalOrgan_PitchHz"),
    wxT("WindSupply_OutputWindCompartmentID")};
  outFilter[WX_LAYER] = {
    WX_LAYER_ID, WX_PIPE_ID, wxT("AmpLvl_LevelAdjustDecibels")};
  outFilter[WX_ATTACK] = {WX_LAYER_ID, WX_SAMPLE_ID};
  outFilter[WX_RELEASE] = {
    WX_LAYER_ID,
    WX_SAMPLE_ID,
    wxT("ReleaseSelCriteria_LatestKeyReleaseTimeMs")};
  outFilter[WX_SAMPLE]
    = {WX_SAMPLE_ID, wxT("InstallationPackageID"), wxT("SampleFilename")};
}

GOHauptwerkToOdf::GOHauptwerkToOdf(
  const GOHauptwerkOdf &odf, const wxString &sampleSetPath)
  : r_Odf(odf),
    m_SampleSetPath(sampleSetPath),
    m_DrawstopCols(12),
    m_DrawstopRows(12) {}

void GOHauptwerkToOdf::PlaceDrawstop(const wxString &group, unsigned stopI) {
  const unsigned nCells = m_DrawstopCols * m_DrawstopRows;

  // Past the main grid GrandOrgue keeps going in the extra rows, which it
  // addresses with row numbers above 99.
  if (stopI < nCells) {
    Set(group, wxT("DispDrawstopRow"), (long)(stopI / m_DrawstopCols + 1));
    Set(group, wxT("DispDrawstopCol"), (long)(stopI % m_DrawstopCols + 1));
  } else {
    const unsigned extraI = stopI - nCells;

    Set(group, wxT("DispDrawstopRow"), (long)(100 + extraI / 6));
    Set(group, wxT("DispDrawstopCol"), (long)(extraI % 6 + 1));
  }
}

void GOHauptwerkToOdf::Set(
  const wxString &group, const wxString &key, const wxString &value) {
  m_Entries[group][key] = value;
}

void GOHauptwerkToOdf::Set(
  const wxString &group, const wxString &key, long value) {
  m_Entries[group][key] = wxString::Format(wxT("%ld"), value);
}

void GOHauptwerkToOdf::Warn(const wxString &message) {
  // One line per distinct problem is useful; ten thousand lines is not.
  if (m_Warnings.size() < 50)
    m_Warnings.push_back(message);
  else if (m_Warnings.size() == 50)
    m_Warnings.push_back(_("... further warnings suppressed"));
}

wxString GOHauptwerkToOdf::ResolveSamplePath(
  const wxString &hwFileName, long installPackageId) const {
  wxString result;

  if (!hwFileName.IsEmpty()) {
    wxString relative = hwFileName;

    relative.Replace(wxT("\\"), wxT("/"));
    while (relative.StartsWith(wxT("/")))
      relative = relative.Mid(1);

    const wxString packageDir
      = wxString::Format(wxT("OrganInstallationPackages/%06ld"), installPackageId);
    const wxString full = m_SampleSetPath + wxT("/") + packageDir + wxT("/")
      + relative;

    // Hauptwerk sets are authored on Windows, so the case recorded in the
    // definition need not match the case on disk. That is harmless there and
    // fatal on Linux, which is where this matters most.
    if (wxFileName::FileExists(full))
      // GrandOrgue resolves relative to the ODF, which sits in
      // OrganDefinitions - one level below the sample set root.
      result = wxT("../") + packageDir + wxT("/") + relative;
  }
  return result;
}

void GOHauptwerkToOdf::BuildIndexes() {
  for (const GOHauptwerkObject &pipe : r_Odf.GetObjects(WX_PIPE))
    m_PipesByRankId[pipe.GetLong(WX_RANK_ID)].push_back(&pipe);
  for (const GOHauptwerkObject &layer : r_Odf.GetObjects(WX_LAYER))
    m_LayersByPipeId[layer.GetLong(WX_PIPE_ID)].push_back(&layer);
  for (const GOHauptwerkObject &attack : r_Odf.GetObjects(WX_ATTACK))
    m_AttacksByLayerId[attack.GetLong(WX_LAYER_ID)].push_back(&attack);
  for (const GOHauptwerkObject &release : r_Odf.GetObjects(WX_RELEASE))
    m_ReleasesByLayerId[release.GetLong(WX_LAYER_ID)].push_back(&release);
}

void GOHauptwerkToOdf::BuildOrgan() {
  const std::vector<GOHauptwerkObject> &generals = r_Odf.GetObjects(WX_GENERAL);

  if (!generals.empty()) {
    const GOHauptwerkObject &g = generals[0];

    Set(WX_ORGAN, wxT("ChurchName"), g.Get(wxT("Identification_Name")));
    Set(WX_ORGAN, wxT("ChurchAddress"), g.Get(wxT("OrganInfo_Location")));
    Set(WX_ORGAN, wxT("OrganBuilder"), g.Get(wxT("OrganInfo_Builder")));
    Set(WX_ORGAN, wxT("OrganBuildDate"), g.Get(wxT("OrganInfo_BuildDate")));
    Set(WX_ORGAN, wxT("OrganComments"), g.Get(wxT("OrganInfo_Comments")));
  }
  Set(WX_ORGAN, wxT("RecordingDetails"), wxEmptyString);
  Set(WX_ORGAN, wxT("DivisionalsStoreIntermanualCouplers"), WX_ODF_NO);
  Set(WX_ORGAN, wxT("DivisionalsStoreIntramanualCouplers"), WX_ODF_NO);
  Set(WX_ORGAN, wxT("DivisionalsStoreTremulants"), WX_ODF_NO);
  Set(WX_ORGAN, wxT("GeneralsStoreDivisionalCouplers"), WX_ODF_NO);
  Set(WX_ORGAN, wxT("NumberOfReversiblePistons"), 0L);
  Set(WX_ORGAN, wxT("NumberOfDivisionalCouplers"), 0L);
  Set(WX_ORGAN, wxT("NumberOfGenerals"), 0L);
}

void GOHauptwerkToOdf::BuildWindchests() {
  unsigned windchestN = 0;

  for (const GOHauptwerkObject &compartment :
       r_Odf.GetObjects(WX_WIND_COMPARTMENT)) {
    const long id = compartment.GetLong(wxT("WindCompartmentID"));
    const wxString group = numbered(wxT("WindchestGroup"), ++windchestN);

    m_WindchestNumberById[id] = windchestN;
    Set(group, WX_NAME, compartment.Get(WX_NAME));
    // Enclosures and tremulants are attached through objects this pass does
    // not read yet, so the windchest starts unmodulated.
    Set(group, wxT("NumberOfEnclosures"), 0L);
    Set(group, wxT("NumberOfTremulants"), 0L);
  }
  if (windchestN == 0) {
    // A rank must name a windchest, so give the organ one to hang off.
    Set(numbered(wxT("WindchestGroup"), 1), WX_NAME, _("Default"));
    Set(numbered(wxT("WindchestGroup"), 1), wxT("NumberOfEnclosures"), 0L);
    Set(numbered(wxT("WindchestGroup"), 1), wxT("NumberOfTremulants"), 0L);
    windchestN = 1;
  }
  Set(WX_ORGAN, wxT("NumberOfWindchestGroups"), (long)windchestN);
}

void GOHauptwerkToOdf::BuildManuals() {
  const std::vector<GOHauptwerkObject> &divisions
    = r_Odf.GetObjects(WX_DIVISION);
  // GrandOrgue numbers the pedal Manual000 and the manuals from 001, and
  // NumberOfManuals counts only the latter. Hauptwerk just lists divisions,
  // so the pedal has to be recognised by name - it is conventionally first,
  // but the name is what actually says so, in whichever language.
  long pedalDivisionId = -1;

  for (const GOHauptwerkObject &division : divisions) {
    const wxString lowerName = division.Get(WX_NAME).Lower();

    if (
      pedalDivisionId < 0
      && (lowerName.Contains(wxT("pedal")) || lowerName.Contains(wxT("pédale"))
          || lowerName.Contains(wxT("dale")) || lowerName.Contains(wxT("pedaal"))))
      pedalDivisionId = division.GetLong(WX_DIVISION_ID);
  }

  const bool hasPedals = pedalDivisionId >= 0;
  unsigned manualN = 0;

  // DivisionInput lists the notes a division actually accepts. It is worth
  // reading rather than assuming a compass: a division that receives couplers
  // is often extended past the keys the player can reach - the Grand Orgue of
  // the benchmark set spans 73 notes where its keyboard has 61 - and assuming
  // 61 would silently drop the top octave.
  std::unordered_map<long, std::pair<long, long>> compassByDivisionId;

  for (const GOHauptwerkObject &input : r_Odf.GetObjects(WX_DIVISION_INPUT)) {
    const long divisionId = input.GetLong(WX_DIVISION_ID);
    const long note = input.GetLong(wxT("NormalMIDINoteNumber"), -1);

    if (note >= 0) {
      const auto it = compassByDivisionId.find(divisionId);

      if (it == compassByDivisionId.end())
        compassByDivisionId[divisionId] = std::make_pair(note, note);
      else {
        it->second.first = std::min(it->second.first, note);
        it->second.second = std::max(it->second.second, note);
      }
    }
  }

  for (const GOHauptwerkObject &division : divisions) {
    const long divisionId = division.GetLong(WX_DIVISION_ID);
    const bool isPedal = divisionId == pedalDivisionId;
    const unsigned number = isPedal ? 0 : ++manualN;
    const wxString group = numbered(wxT("Manual"), number);
    // A pedalboard is 32 notes from C; a manual 61 from C. Hauptwerk states
    // the compass per key action rather than per division, and a key with no
    // pipe is simply silent, so the conventional compass is safe here.
    const auto compassIt = compassByDivisionId.find(divisionId);
    long firstNote = 36;
    long nLogicalKeys = isPedal ? 32L : 61L;

    if (compassIt != compassByDivisionId.end()) {
      firstNote = compassIt->second.first;
      nLogicalKeys = compassIt->second.second - firstNote + 1;
    }

    // The extended range exists for couplers, not for fingers, so the player
    // gets the conventional compass and the rest stays reachable by coupling.
    const long nAccessibleKeys
      = std::min(nLogicalKeys, isPedal ? 32L : 61L);

    m_ManualNumberByDivisionId[divisionId] = number;
    Set(group, WX_NAME, division.Get(WX_NAME));
    Set(group, wxT("NumberOfLogicalKeys"), nLogicalKeys);
    Set(group, wxT("FirstAccessibleKeyLogicalKeyNumber"), 1L);
    Set(group, wxT("FirstAccessibleKeyMIDINoteNumber"), firstNote);
    Set(group, wxT("NumberOfAccessibleKeys"), nAccessibleKeys);
    Set(group, wxT("NumberOfCouplers"), 0L);
    Set(group, wxT("NumberOfDivisionals"), 0L);
    Set(group, wxT("NumberOfTremulants"), 0L);
    Set(group, wxT("NumberOfSwitches"), 0L);
    // GrandOrgue always builds its own main panel, whatever NumberOfPanels
    // says, so the manuals have to be marked displayed or that panel comes up
    // without a keyboard on it.
    Set(group, wxT("Displayed"), WX_ODF_YES);
    // filled by BuildStops
    Set(group, wxT("NumberOfStops"), 0L);
  }
  Set(WX_ORGAN, wxT("NumberOfManuals"), (long)manualN);
  Set(WX_ORGAN, wxT("HasPedals"), hasPedals ? WX_ODF_YES : WX_ODF_NO);
  Set(WX_ORGAN, wxT("NumberOfSwitches"), 0L);
  Set(WX_ORGAN, wxT("NumberOfPanels"), 0L);
}

void GOHauptwerkToOdf::BuildRank(
  const GOHauptwerkObject &rank, unsigned rankN) {
  const wxString group = numbered(wxT("Rank"), rankN);
  const long rankId = rank.GetLong(WX_RANK_ID);
  const auto pipesIt = m_PipesByRankId.find(rankId);
  std::vector<const GOHauptwerkObject *> pipes;

  if (pipesIt != m_PipesByRankId.end())
    pipes = pipesIt->second;

  std::sort(
    pipes.begin(),
    pipes.end(),
    [](const GOHauptwerkObject *a, const GOHauptwerkObject *b) {
      return a->GetLong(wxT("NormalMIDINoteNumber"))
        < b->GetLong(wxT("NormalMIDINoteNumber"));
    });

  long firstMidiNote = 36;
  unsigned windchestN = 1;

  if (!pipes.empty()) {
    firstMidiNote = pipes[0]->GetLong(wxT("NormalMIDINoteNumber"), 36);

    const auto wcIt = m_WindchestNumberById.find(
      pipes[0]->GetLong(wxT("WindSupply_OutputWindCompartmentID")));

    if (wcIt != m_WindchestNumberById.end())
      windchestN = wcIt->second;
  }

  Set(group, WX_NAME, rank.Get(WX_NAME));
  Set(group, wxT("FirstMidiNoteNumber"), firstMidiNote);
  Set(group, wxT("NumberOfLogicalPipes"), (long)pipes.size());
  Set(group, wxT("WindchestGroup"), (long)windchestN);
  Set(group, wxT("Percussive"), WX_ODF_NO);
  Set(group, wxT("AcceptsRetuning"), WX_ODF_YES);

  for (unsigned n = pipes.size(), pipeI = 0; pipeI < n; pipeI++) {
    const GOHauptwerkObject &pipe = *pipes[pipeI];
    const wxString pipeKey = wxString::Format(wxT("Pipe%03u"), pipeI + 1);
    const auto layersIt = m_LayersByPipeId.find(pipe.GetLong(WX_PIPE_ID));
    wxString attackPath;
    std::vector<std::pair<wxString, long>> releases;

    if (layersIt != m_LayersByPipeId.end())
      for (const GOHauptwerkObject *pLayer : layersIt->second) {
        const long layerId = pLayer->GetLong(WX_LAYER_ID);
        const auto attacksIt = m_AttacksByLayerId.find(layerId);

        if (attacksIt != m_AttacksByLayerId.end())
          for (const GOHauptwerkObject *pAttack : attacksIt->second) {
            const GOHauptwerkObject *pSample = r_Odf.FindById(
              WX_SAMPLE, WX_SAMPLE_ID, pAttack->GetLong(WX_SAMPLE_ID));

            if (pSample && attackPath.IsEmpty())
              attackPath = ResolveSamplePath(
                pSample->Get(wxT("SampleFilename")),
                pSample->GetLong(wxT("InstallationPackageID")));
          }

        const auto releasesIt = m_ReleasesByLayerId.find(layerId);

        if (releasesIt != m_ReleasesByLayerId.end())
          for (const GOHauptwerkObject *pRelease : releasesIt->second) {
            const GOHauptwerkObject *pSample = r_Odf.FindById(
              WX_SAMPLE, WX_SAMPLE_ID, pRelease->GetLong(WX_SAMPLE_ID));

            if (pSample) {
              const wxString path = ResolveSamplePath(
                pSample->Get(wxT("SampleFilename")),
                pSample->GetLong(wxT("InstallationPackageID")));

              if (!path.IsEmpty())
                releases.emplace_back(
                  path,
                  pRelease->GetLong(
                    wxT("ReleaseSelCriteria_LatestKeyReleaseTimeMs"), -1));
            }
          }
      }

    if (attackPath.IsEmpty()) {
      Warn(wxString::Format(
        _("Rank \"%s\": no usable attack sample for MIDI note %ld"),
        rank.Get(WX_NAME),
        pipe.GetLong(wxT("NormalMIDINoteNumber"))));
      Set(group, pipeKey, wxT("DUMMY"));
    } else {
      Set(group, pipeKey, attackPath);

      const long harmonic
        = pipe.GetLong(wxT("Pitch_Tempered_RankBasePitch64ftHarmonicNum"), 0);

      if (harmonic > 0)
        Set(group, pipeKey + wxT("HarmonicNumber"), harmonic);

      // Hauptwerk picks a release by how long the key was held, so the
      // shortest limit has to be tried first; the file does not list them in
      // that order. The longest one carries a sentinel rather than a real
      // limit (99999 ms), and GrandOrgue expects no MaxKeyPressTime at all
      // for the release that catches everything else.
      std::sort(
        releases.begin(),
        releases.end(),
        [](const std::pair<wxString, long> &a,
           const std::pair<wxString, long> &b) { return a.second < b.second; });

      Set(group, pipeKey + wxT("ReleaseCount"), (long)releases.size());
      for (unsigned nReleases = releases.size(), relI = 0; relI < nReleases;
           relI++) {
        const wxString relKey
          = wxString::Format(wxT("%sRelease%03u"), pipeKey, relI + 1);
        const long maxKeyPressTime = releases[relI].second;

        Set(group, relKey, releases[relI].first);
        if (
          nReleases > 1 && maxKeyPressTime >= 0
          && maxKeyPressTime < HW_UNLIMITED_KEY_PRESS_MS)
          Set(group, relKey + wxT("MaxKeyPressTime"), maxKeyPressTime);
      }
    }
  }
}

void GOHauptwerkToOdf::BuildRanks() {
  const std::vector<GOHauptwerkObject> &ranks = r_Odf.GetObjects(WX_RANK);
  unsigned rankN = 0;

  for (const GOHauptwerkObject &rank : ranks) {
    const long rankId = rank.GetLong(WX_RANK_ID);
    const auto pipesIt = m_PipesByRankId.find(rankId);

    // A sample set declares a rank for every microphone perspective it could
    // offer and leaves the ones it does not ship without pipes - two thirds of
    // them in the benchmark set. GrandOrgue rejects a rank of no pipes, so
    // those are dropped rather than emitted empty. Numbering follows the ranks
    // actually written, so stop references stay correct.
    if (pipesIt != m_PipesByRankId.end() && !pipesIt->second.empty()) {
      m_RankNumberById[rankId] = ++rankN;
      BuildRank(rank, rankN);
    }
  }
  Set(WX_ORGAN, wxT("NumberOfRanks"), (long)rankN);
}

void GOHauptwerkToOdf::BuildStops() {
  std::unordered_map<long, std::vector<const GOHauptwerkObject *>>
    stopRanksByStopId;

  for (const GOHauptwerkObject &stopRank : r_Odf.GetObjects(WX_STOP_RANK))
    stopRanksByStopId[stopRank.GetLong(WX_STOP_ID)].push_back(&stopRank);

  std::unordered_map<unsigned, unsigned> stopCountByManual;
  unsigned stopN = 0;

  for (const GOHauptwerkObject &stop : r_Odf.GetObjects(WX_STOP)) {
    const auto manualIt
      = m_ManualNumberByDivisionId.find(stop.GetLong(WX_DIVISION_ID));
    const auto ranksIt = stopRanksByStopId.find(stop.GetLong(WX_STOP_ID));

    if (manualIt == m_ManualNumberByDivisionId.end())
      Warn(wxString::Format(
        _("Stop \"%s\" names a division that does not exist"),
        stop.Get(WX_NAME)));
    else if (ranksIt != stopRanksByStopId.end()) {
      // A stop with no ranks controls nothing GrandOrgue can sound, so it is
      // skipped rather than emitted as an empty drawstop.
      const wxString group = numbered(wxT("Stop"), ++stopN);
      unsigned rankRefN = 0;
      long accessiblePipes = 0;

      Set(group, WX_NAME, stop.Get(WX_NAME));
      Set(group, wxT("FirstAccessiblePipeLogicalKeyNumber"), 1L);
      // Without this the drawstop is not drawn at all and there is no way to
      // engage the stop: GrandOrgue defaults Displayed to N.
      Set(group, wxT("Displayed"), WX_ODF_YES);
      // GODrawstop reads this as a required value for a plain drawstop - one
      // with no Function - so it must be present even though N is what a stop
      // starts as anyway.
      Set(group, wxT("DefaultToEngaged"), WX_ODF_NO);
      PlaceDrawstop(group, stopN - 1);

      for (const GOHauptwerkObject *pStopRank : ranksIt->second) {
        const auto rankIt
          = m_RankNumberById.find(pStopRank->GetLong(WX_RANK_ID));

        if (rankIt != m_RankNumberById.end()) {
          const wxString refKey = wxString::Format(wxT("Rank%03u"), ++rankRefN);

          Set(group, refKey, (long)rankIt->second);
          accessiblePipes = std::max(
            accessiblePipes,
            pStopRank->GetLong(wxT("NumberOfMappedDivisionInputNodes"), 0));
        }
      }
      if (rankRefN == 0) {
        // Every rank this stop names was dropped for having no pipes, so the
        // stop would be an empty drawstop. Take the number back and forget it.
        m_Entries.erase(group);
        stopN--;
      } else {
        Set(group, wxT("NumberOfRanks"), (long)rankRefN);
        Set(
          group,
          wxT("NumberOfAccessiblePipes"),
          accessiblePipes > 0 ? accessiblePipes : 61L);

        const unsigned manualN = manualIt->second;
        const unsigned manualStopN = ++stopCountByManual[manualN];
        const wxString manualGroup = numbered(wxT("Manual"), manualN);

        Set(
          manualGroup,
          wxString::Format(wxT("Stop%03u"), manualStopN),
          (long)stopN);
        Set(manualGroup, wxT("NumberOfStops"), (long)manualStopN);
      }
    }
  }
  Set(WX_ORGAN, wxT("NumberOfStops"), (long)stopN);
}


void GOHauptwerkToOdf::BuildCouplers() {
  // Hauptwerk states a key action between keyboards; GrandOrgue states a
  // coupler on the source manual naming the destination one. Keyboards carry
  // only a hint of which division they play, which is the only link between
  // the two numberings.
  for (const GOHauptwerkObject &keyboard : r_Odf.GetObjects(WX_KEYBOARD)) {
    const auto manualIt = m_ManualNumberByDivisionId.find(
      keyboard.GetLong(wxT("Hint_PrimaryAssociatedDivisionID")));

    if (manualIt != m_ManualNumberByDivisionId.end())
      m_ManualNumberByKeyboardId[keyboard.GetLong(wxT("KeyboardID"))]
        = manualIt->second;
  }

  std::unordered_map<unsigned, unsigned> couplerCountByManual;
  unsigned couplerN = 0;

  for (const GOHauptwerkObject &action : r_Odf.GetObjects(WX_KEY_ACTION)) {
    const auto srcIt = m_ManualNumberByKeyboardId.find(
      action.GetLong(wxT("SourceKeyboardID")));
    const auto dstIt
      = m_ManualNumberByKeyboardId.find(action.GetLong(wxT("DestKeyboardID")));

    // A key action to or from a keyboard with no division of its own - the
    // extra "GrandOrgue Ex" style keyboards sets add - has nothing to couple.
    if (
      srcIt != m_ManualNumberByKeyboardId.end()
      && dstIt != m_ManualNumberByKeyboardId.end()
      && srcIt->second != dstIt->second) {
      const wxString group = numbered(wxT("Coupler"), ++couplerN);

      Set(group, WX_NAME, action.Get(WX_NAME));
      Set(group, wxT("UnisonOff"), WX_ODF_NO);
      Set(group, wxT("DestinationManual"), (long)dstIt->second);
      Set(
        group,
        wxT("DestinationKeyshift"),
        action.GetLong(wxT("MIDINoteNumberIncrement"), 0));
      Set(group, wxT("CoupleToSubsequentUnisonIntermanualCouplers"), WX_ODF_NO);
      Set(group, wxT("CoupleToSubsequentUpwardIntermanualCouplers"), WX_ODF_NO);
      Set(
        group, wxT("CoupleToSubsequentDownwardIntermanualCouplers"), WX_ODF_NO);
      Set(group, wxT("CoupleToSubsequentUpwardIntramanualCouplers"), WX_ODF_NO);
      Set(
        group, wxT("CoupleToSubsequentDownwardIntramanualCouplers"), WX_ODF_NO);
      Set(group, wxT("Displayed"), WX_ODF_YES);
      Set(group, wxT("DefaultToEngaged"), WX_ODF_NO);

      const unsigned srcManualN = srcIt->second;
      const unsigned manualCouplerN = ++couplerCountByManual[srcManualN];
      const wxString manualGroup = numbered(wxT("Manual"), srcManualN);

      Set(
        manualGroup,
        wxString::Format(wxT("Coupler%03u"), manualCouplerN),
        (long)couplerN);
      Set(manualGroup, wxT("NumberOfCouplers"), (long)manualCouplerN);
    }
  }
}

void GOHauptwerkToOdf::BuildTremulants() {
  unsigned tremulantN = 0;

  for (const GOHauptwerkObject &tremulant : r_Odf.GetObjects(WX_TREMULANT)) {
    const wxString group = numbered(wxT("Tremulant"), ++tremulantN);
    const double freqHz = wxAtof(tremulant.Get(wxT("FrequencyWhenEngagedHz")));
    // Hauptwerk gives a frequency, GrandOrgue a period in milliseconds.
    const long periodMs = freqHz > 0.1 ? (long)(1000.0 / freqHz) : 200L;

    m_TremulantNumberById[tremulant.GetLong(wxT("TremulantID"))] = tremulantN;
    Set(group, WX_NAME, tremulant.Get(WX_NAME));
    Set(group, wxT("Period"), periodMs);
    // Hauptwerk carries the depth in the sampled waveform rather than as a
    // number, so this is GrandOrgue's synthesised approximation of it.
    Set(group, wxT("AmpModDepth"), 10L);
    Set(
      group,
      wxT("StartRate"),
      tremulant.GetLong(wxT("StartRatePercent"), 30));
    Set(group, wxT("StopRate"), tremulant.GetLong(wxT("StopRatePercent"), 30));
    Set(group, wxT("Displayed"), WX_ODF_YES);
    Set(group, wxT("DefaultToEngaged"), WX_ODF_NO);
    // Which windchests it acts on is not stated directly; a tremulant belongs
    // to a division, so it is attached to every windchest the organ has.
    for (const auto &pair : m_WindchestNumberById)
      m_WindchestsByTremulantN[tremulantN].insert(pair.second);
  }
  Set(WX_ORGAN, wxT("NumberOfTremulants"), (long)tremulantN);
}

void GOHauptwerkToOdf::BuildEnclosures() {
  unsigned enclosureN = 0;

  for (const GOHauptwerkObject &enclosure : r_Odf.GetObjects(WX_ENCLOSURE)) {
    const wxString group = numbered(wxT("Enclosure"), ++enclosureN);

    m_EnclosureNumberById[enclosure.GetLong(wxT("EnclosureID"))] = enclosureN;
    Set(group, WX_NAME, enclosure.Get(WX_NAME));
    Set(group, wxT("DispLabelText"), enclosure.Get(WX_NAME));
    Set(group, wxT("AmpMinimumLevel"), 0L);
    Set(group, wxT("MIDIInputNumber"), (long)enclosureN);
  }
  Set(WX_ORGAN, wxT("NumberOfEnclosures"), (long)enclosureN);

  // EnclosurePipe says which pipes a box encloses. GrandOrgue encloses whole
  // windchests, so a box takes in every windchest any of its pipes sits on.
  for (const GOHauptwerkObject &pipe : r_Odf.GetObjects(WX_PIPE))
    m_WindchestNumberByPipeId[pipe.GetLong(WX_PIPE_ID)]
      = m_WindchestNumberById.count(
          pipe.GetLong(wxT("WindSupply_OutputWindCompartmentID")))
      ? m_WindchestNumberById[pipe.GetLong(
          wxT("WindSupply_OutputWindCompartmentID"))]
      : 1;

  for (const GOHauptwerkObject &ep : r_Odf.GetObjects(WX_ENCLOSURE_PIPE)) {
    const auto encIt = m_EnclosureNumberById.find(ep.GetLong(wxT("EnclosureID")));
    const auto wcIt = m_WindchestNumberByPipeId.find(ep.GetLong(WX_PIPE_ID));

    if (
      encIt != m_EnclosureNumberById.end()
      && wcIt != m_WindchestNumberByPipeId.end())
      m_WindchestsByEnclosureN[encIt->second].insert(wcIt->second);
  }

  // Now that both are known, write the references onto the windchests.
  for (const auto &pair : m_WindchestNumberById) {
    const unsigned windchestN = pair.second;
    const wxString group = numbered(wxT("WindchestGroup"), windchestN);
    unsigned refN = 0;

    for (const auto &enc : m_WindchestsByEnclosureN)
      if (enc.second.count(windchestN))
        Set(
          group,
          wxString::Format(wxT("Enclosure%03u"), ++refN),
          (long)enc.first);
    Set(group, wxT("NumberOfEnclosures"), (long)refN);

    refN = 0;
    for (const auto &trem : m_WindchestsByTremulantN)
      if (trem.second.count(windchestN))
        Set(
          group,
          wxString::Format(wxT("Tremulant%03u"), ++refN),
          (long)trem.first);
    Set(group, wxT("NumberOfTremulants"), (long)refN);
  }
}

void GOHauptwerkToOdf::BuildDefaultConsole(unsigned nStops, unsigned nManuals) {
  // GrandOrgue draws its own console whatever NumberOfPanels says, but it
  // lays it out from these settings, and their defaults assume a small organ.
  // A set with 140 stops gets a console with nowhere to put them unless the
  // grid is sized here. The Hauptwerk console graphics are not reproduced -
  // this is GrandOrgue's own generic layout, sized to fit.
  const unsigned nCols = 12;

  // Two drawstop columns flank each side of the manuals, so the grid holds
  // roughly nCols * nRows; round the rows up and leave a margin.
  unsigned nRows = (nStops + nCols - 1) / nCols + 2;

  if (nRows < 8)
    nRows = 8;
  if (nRows > 20)
    nRows = 20;

  Set(WX_ORGAN, wxT("DispScreenSizeHoriz"), wxT("1900"));
  Set(WX_ORGAN, wxT("DispScreenSizeVert"), wxT("980"));
  m_DrawstopCols = nCols;
  m_DrawstopRows = nRows;
  Set(WX_ORGAN, wxT("DispDrawstopCols"), (long)nCols);
  Set(WX_ORGAN, wxT("DispDrawstopRows"), (long)nRows);
  Set(WX_ORGAN, wxT("DispExtraDrawstopCols"), 6L);
  Set(WX_ORGAN, wxT("DispExtraDrawstopRows"), 5L);
  Set(WX_ORGAN, wxT("DispExtraDrawstopRowsAboveExtraButtonRows"), WX_ODF_YES);
  Set(WX_ORGAN, wxT("DispButtonCols"), 10L);
  Set(WX_ORGAN, wxT("DispExtraButtonRows"), 0L);
  Set(WX_ORGAN, wxT("DispButtonsAboveManuals"), WX_ODF_NO);
  Set(WX_ORGAN, wxT("DispDrawstopColsOffset"), WX_ODF_NO);
  Set(WX_ORGAN, wxT("DispDrawstopOuterColOffsetUp"), WX_ODF_NO);
  Set(WX_ORGAN, wxT("DispPairDrawstopCols"), WX_ODF_NO);
  Set(WX_ORGAN, wxT("DispExtraPedalButtonRow"), WX_ODF_NO);
  Set(WX_ORGAN, wxT("DispExtraPedalButtonRowOffset"), WX_ODF_NO);
  Set(WX_ORGAN, wxT("DispExtraPedalButtonRowOffsetRight"), WX_ODF_NO);
  Set(WX_ORGAN, wxT("DispTrimAboveManuals"), nManuals > 1 ? WX_ODF_YES : WX_ODF_NO);
  Set(WX_ORGAN, wxT("DispTrimBelowManuals"), WX_ODF_NO);
  Set(WX_ORGAN, wxT("DispTrimAboveExtraRows"), WX_ODF_NO);
  Set(WX_ORGAN, wxT("DispControlLabelFont"), wxT("Arial"));
  Set(WX_ORGAN, wxT("DispGroupLabelFont"), wxT("Arial"));
  Set(WX_ORGAN, wxT("DispShortcutKeyLabelFont"), wxT("Arial"));
  Set(WX_ORGAN, wxT("DispShortcutKeyLabelColour"), wxT("Black"));
  Set(WX_ORGAN, wxT("DispConsoleBackgroundImageNum"), 1L);
  Set(WX_ORGAN, wxT("DispDrawstopBackgroundImageNum"), 1L);
  Set(WX_ORGAN, wxT("DispDrawstopInsetBackgroundImageNum"), 1L);
  Set(WX_ORGAN, wxT("DispKeyHorizBackgroundImageNum"), 1L);
  Set(WX_ORGAN, wxT("DispKeyVertBackgroundImageNum"), 1L);
  Set(WX_ORGAN, wxT("CombinationsStoreNonDisplayedDrawstops"), WX_ODF_NO);
  Set(WX_ORGAN, wxT("NumberOfImages"), 0L);
  Set(WX_ORGAN, wxT("NumberOfLabels"), 0L);
  Set(WX_ORGAN, wxT("InfoFilename"), wxEmptyString);
  Set(WX_ORGAN, wxT("AmplitudeLevel"), 100L);
  Set(WX_ORGAN, wxT("Gain"), 0L);
  Set(WX_ORGAN, wxT("PitchTuning"), 0L);
}

void GOHauptwerkToOdf::Build() {
  BuildIndexes();
  BuildOrgan();
  BuildWindchests();
  BuildManuals();
  BuildRanks();
  BuildDefaultConsole(
    r_Odf.GetObjectCount(WX_STOP), r_Odf.GetObjectCount(WX_DIVISION));
  BuildStops();
  BuildCouplers();
  BuildTremulants();
  BuildEnclosures();
}
