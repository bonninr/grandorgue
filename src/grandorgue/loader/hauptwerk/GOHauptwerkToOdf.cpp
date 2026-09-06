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

// Hauptwerk attribute names used in more than one place
static const wxString WX_NAME = wxT("Name");
static const wxString WX_RANK_ID = wxT("RankID");
static const wxString WX_PIPE_ID = wxT("PipeID");
static const wxString WX_LAYER_ID = wxT("LayerID");
static const wxString WX_SAMPLE_ID = wxT("SampleID");
static const wxString WX_STOP_ID = wxT("StopID");
static const wxString WX_DIVISION_ID = wxT("DivisionID");

static const wxString WX_ORGAN = wxT("Organ");
static const wxString WX_ODF_YES = wxT("Y");
static const wxString WX_ODF_NO = wxT("N");

/** Group name with the three-digit suffix GrandOrgue ODFs use. */
static wxString numbered(const wxString &prefix, unsigned n) {
  return wxString::Format(wxT("%s%03u"), prefix, n);
}

void GOHauptwerkToOdf::FillReadFilter(
  std::unordered_map<wxString, std::unordered_set<wxString>> &outFilter) {
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
  : r_Odf(odf), m_SampleSetPath(sampleSetPath) {}

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
  unsigned manualN = 0;
  bool hasPedals = false;

  for (const GOHauptwerkObject &division : divisions) {
    const long divisionId = division.GetLong(WX_DIVISION_ID);
    const wxString group = numbered(wxT("Manual"), ++manualN);
    const wxString name = division.Get(WX_NAME);

    m_ManualNumberByDivisionId[divisionId] = manualN;
    Set(group, WX_NAME, name);
    // Hauptwerk describes the compass per key action rather than per
    // division; 36..96 covers every division of a normal organ and unused
    // keys simply have no pipes.
    Set(group, wxT("NumberOfLogicalKeys"), 61L);
    Set(group, wxT("FirstAccessibleKeyLogicalKeyNumber"), 1L);
    Set(group, wxT("FirstAccessibleKeyMIDINoteNumber"), 36L);
    Set(group, wxT("NumberOfAccessibleKeys"), 61L);
    Set(group, wxT("NumberOfCouplers"), 0L);
    Set(group, wxT("NumberOfDivisionals"), 0L);
    Set(group, wxT("NumberOfTremulants"), 0L);
    Set(group, wxT("NumberOfSwitches"), 0L);
    Set(group, wxT("Displayed"), WX_ODF_NO);
    // filled by BuildStops
    Set(group, wxT("NumberOfStops"), 0L);

    if (manualN == 1 && name.Lower().Contains(wxT("dale")))
      hasPedals = true;
  }
  Set(WX_ORGAN, wxT("NumberOfManuals"), (long)manualN);
  Set(WX_ORGAN, wxT("HasPedals"), hasPedals ? WX_ODF_YES : WX_ODF_NO);
  Set(WX_ORGAN, wxT("NumberOfEnclosures"), 0L);
  Set(WX_ORGAN, wxT("NumberOfTremulants"), 0L);
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

      Set(group, pipeKey + wxT("ReleaseCount"), (long)releases.size());
      for (unsigned relN = 0; relN < releases.size(); relN++) {
        const wxString relKey
          = wxString::Format(wxT("%sRelease%03u"), pipeKey, relN + 1);

        Set(group, relKey, releases[relN].first);
        if (releases[relN].second >= 0 && releases.size() > 1)
          Set(group, relKey + wxT("MaxKeyPressTime"), releases[relN].second);
      }
    }
  }
}

void GOHauptwerkToOdf::BuildRanks() {
  const std::vector<GOHauptwerkObject> &ranks = r_Odf.GetObjects(WX_RANK);
  unsigned rankN = 0;

  for (const GOHauptwerkObject &rank : ranks) {
    m_RankNumberById[rank.GetLong(WX_RANK_ID)] = ++rankN;
    BuildRank(rank, rankN);
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
      Set(group, wxT("NumberOfRanks"), (long)rankRefN);
      Set(
        group,
        wxT("NumberOfAccessiblePipes"),
        accessiblePipes > 0 ? accessiblePipes : 61L);

      const unsigned manualN = manualIt->second;
      const unsigned manualStopN = ++stopCountByManual[manualN];
      const wxString manualGroup = numbered(wxT("Manual"), manualN);

      Set(
        manualGroup, wxString::Format(wxT("Stop%03u"), manualStopN), (long)stopN);
      Set(manualGroup, wxT("NumberOfStops"), (long)manualStopN);
    }
  }
  Set(WX_ORGAN, wxT("NumberOfStops"), (long)stopN);
}

void GOHauptwerkToOdf::Build() {
  BuildIndexes();
  BuildOrgan();
  BuildWindchests();
  BuildManuals();
  BuildRanks();
  BuildStops();
}
