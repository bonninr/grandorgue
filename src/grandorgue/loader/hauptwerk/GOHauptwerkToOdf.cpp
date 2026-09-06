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
#include <cmath>

#include "GOHauptwerkOdf.h"

// Hauptwerk object types
static const wxString WX_GENERAL = wxT("_General");
static const wxString WX_DIVISION = wxT("Division");
static const wxString WX_KEYBOARD = wxT("Keyboard");
static const wxString WX_KEYBOARD_KEY = wxT("KeyboardKey");
static const wxString WX_REQUIRED_INSTALLATION_PACKAGE
  = wxT("RequiredInstallationPackage");
static const wxString WX_STOP = wxT("Stop");
static const wxString WX_STOP_RANK = wxT("StopRank");
static const wxString WX_RANK = wxT("Rank");
static const wxString WX_PIPE = wxT("Pipe_SoundEngine01");
static const wxString WX_LAYER = wxT("Pipe_SoundEngine01_Layer");
static const wxString WX_ATTACK = wxT("Pipe_SoundEngine01_AttackSample");
static const wxString WX_RELEASE = wxT("Pipe_SoundEngine01_ReleaseSample");
static const wxString WX_SAMPLE = wxT("Sample");
static const wxString WX_WIND_COMPARTMENT = wxT("WindCompartment");
static const wxString WX_WIND_COMPARTMENT_LINKAGE
  = wxT("WindCompartmentLinkage");
static const wxString WX_TREMULANT = wxT("Tremulant");
static const wxString WX_TREMULANT_WAVEFORM = wxT("TremulantWaveform");
static const wxString WX_TREMULANT_WAVEFORM_PIPE = wxT("TremulantWaveformPipe");
static const wxString WX_ENCLOSURE = wxT("Enclosure");
static const wxString WX_ENCLOSURE_PIPE = wxT("EnclosurePipe");
static const wxString WX_KEY_ACTION = wxT("KeyAction");
static const wxString WX_SWITCH = wxT("Switch");
static const wxString WX_SWITCH_LINKAGE = wxT("SwitchLinkage");
static const wxString WX_COMBINATION = wxT("Combination");
static const wxString WX_COMBINATION_ELEMENT = wxT("CombinationElement");
static const wxString WX_DISPLAY_PAGE = wxT("DisplayPage");
static const wxString WX_IMAGE_SET = wxT("ImageSet");
static const wxString WX_IMAGE_SET_ELEMENT = wxT("ImageSetElement");
static const wxString WX_IMAGE_SET_INSTANCE = wxT("ImageSetInstance");
static const wxString WX_DIVISION_INPUT = wxT("DivisionInput");

// Hauptwerk attribute names used in more than one place
static const wxString WX_NAME = wxT("Name");
static const wxString WX_RANK_ID = wxT("RankID");
static const wxString WX_PIPE_ID = wxT("PipeID");
static const wxString WX_LAYER_ID = wxT("LayerID");
static const wxString WX_SAMPLE_ID = wxT("SampleID");
static const wxString WX_STOP_ID = wxT("StopID");
static const wxString WX_SWITCH_ID = wxT("SwitchID");
static const wxString WX_CONTROLLING_SWITCH_ID = wxT("ControllingSwitchID");
static const wxString WX_SOURCE_SWITCH_ID = wxT("SourceSwitchID");
static const wxString WX_DEST_SWITCH_ID = wxT("DestSwitchID");
static const wxString WX_COMBINATION_ID = wxT("CombinationID");
static const wxString WX_INSTALLATION_PACKAGE_ID = wxT("InstallationPackageID");
static const wxString WX_DIVISION_ID = wxT("DivisionID");

// Hauptwerk marks the release that catches every remaining key-press length
// with this instead of a real limit.
static const long HW_UNLIMITED_KEY_PRESS_MS = 99999;
// Link action codes: "engage the destination" and "release the destination"
static const long HW_LINK_ENGAGE = 1;
static const long HW_LINK_DISENGAGE = 2;
// GOOrganModel reads NumberOfSwitches with this as its upper bound
static const unsigned MAX_ODF_SWITCHES = 999;
// GOOrganController reads NumberOfPanels with this as its upper bound
static const unsigned MAX_ODF_PANELS = 100;
// GOSoundingPipe reads either crossfade length with this as its upper bound
static const long MAX_CROSSFADE_MS = 3000;
// Hauptwerk combination type of a crescendo stage
static const long HW_COMBINATION_CRESCENDO = 4;
// Positions of GOSetter's crescendo pedal (CRESCENDO_STEPS there)
static const unsigned N_CRESCENDO_STEPS = 32;
// GOCoupler reads DestinationKeyshift with these as its bounds
static const long MIN_KEYSHIFT = -24;
static const long MAX_KEYSHIFT = 24;
// Where GrandOrgue's synthesised tremulant sits when nothing says otherwise
static const unsigned DEFAULT_TREMULANT_DEPTH = 10;

static const wxString WX_ORGAN = wxT("Organ");
static const wxString WX_ODF_YES = wxT("Y");
static const wxString WX_ODF_NO = wxT("N");

/** Group name with the three-digit suffix GrandOrgue ODFs use. */
/** One release sample, as the rank builder collects it before ordering. */
struct GOHauptwerkRelease {
  wxString path;
  // How long the key may have been held for this release to be the one used
  long maxKeyPressMs;
  // How long to fade the release in over the tail of the attack
  long crossfadeMs;
};

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
  outFilter[WX_KEYBOARD_KEY]
    = {wxT("KeyboardID"), wxT("NormalMIDINoteNumber"), WX_SWITCH_ID};
  outFilter[WX_REQUIRED_INSTALLATION_PACKAGE]
    = {WX_INSTALLATION_PACKAGE_ID, WX_NAME, wxT("SupplierName")};
  outFilter[WX_STOP] = {};
  outFilter[WX_STOP_RANK] = {};
  outFilter[WX_RANK] = {};
  outFilter[WX_WIND_COMPARTMENT] = {};
  outFilter[WX_WIND_COMPARTMENT_LINKAGE] = {
    wxT("SecondWindCompartmentID"),
    wxT("MassFlowRateKilogramsPerSecAtReferencePressureDiff")};
  outFilter[WX_TREMULANT] = {};
  outFilter[WX_TREMULANT_WAVEFORM]
    = {wxT("TremulantWaveformID"), wxT("TremulantID")};
  outFilter[WX_TREMULANT_WAVEFORM_PIPE] = {
    WX_PIPE_ID,
    wxT("TremulantWaveformID"),
    wxT("AmplitudeModDepthAdjustDecibels"),
    wxT("PitchModDepthAdjustPercent")};
  outFilter[WX_ENCLOSURE] = {};
  outFilter[WX_ENCLOSURE_PIPE] = {WX_PIPE_ID, wxT("EnclosureID")};
  outFilter[WX_KEY_ACTION] = {};
  outFilter[WX_SWITCH] = {
    WX_SWITCH_ID,
    WX_NAME,
    wxT("DefaultToEngaged"),
    wxT("Clickable"),
    wxT("Disp_ImageSetInstanceID"),
    wxT("Disp_ImageSetIndexEngaged"),
    wxT("Disp_ImageSetIndexDisengaged")};
  outFilter[WX_DISPLAY_PAGE] = {wxT("PageID"), WX_NAME};
  outFilter[WX_IMAGE_SET] = {
    wxT("ImageSetID"),
    WX_INSTALLATION_PACKAGE_ID,
    wxT("ImageWidthPixels"),
    wxT("ImageHeightPixels"),
    wxT("ClickableAreaLeftRelativeXPosPixels"),
    wxT("ClickableAreaRightRelativeXPosPixels"),
    wxT("ClickableAreaTopRelativeYPosPixels"),
    wxT("ClickableAreaBottomRelativeYPosPixels")};
  outFilter[WX_IMAGE_SET_ELEMENT]
    = {wxT("ImageSetID"), wxT("ImageIndexWithinSet"), wxT("BitmapFilename")};
  outFilter[WX_IMAGE_SET_INSTANCE] = {
    wxT("ImageSetInstanceID"),
    wxT("ImageSetID"),
    wxT("DefaultImageIndexWithinSet"),
    wxT("DisplayPageID"),
    wxT("ScreenLayerNumber"),
    wxT("LeftXPosPixels"),
    wxT("TopYPosPixels")};
  outFilter[WX_COMBINATION] = {WX_COMBINATION_ID, wxT("CombinationTypeCode")};
  outFilter[WX_COMBINATION_ELEMENT] = {
    WX_COMBINATION_ID,
    wxT("ControlledSwitchID"),
    wxT("InitialStoredStateIsEngaged")};
  outFilter[WX_SWITCH_LINKAGE] = {
    WX_SOURCE_SWITCH_ID,
    WX_DEST_SWITCH_ID,
    wxT("EngageLinkActionCode"),
    wxT("DisengageLinkActionCode")};
  outFilter[WX_DIVISION_INPUT]
    = {WX_DIVISION_ID, wxT("NormalMIDINoteNumber"), WX_SWITCH_ID};
  outFilter[WX_PIPE] = {
    WX_PIPE_ID,
    WX_RANK_ID,
    wxT("NormalMIDINoteNumber"),
    wxT("Pitch_Tempered_RankBasePitch64ftHarmonicNum"),
    wxT("Pitch_OriginalOrgan_PitchHz"),
    wxT("WindSupply_SourceWindCompartmentID"),
    wxT("WindSupply_MassFlowRateKilogramsPerSecAtReferencePressureDiff")};
  outFilter[WX_LAYER] = {
    WX_LAYER_ID,
    WX_PIPE_ID,
    wxT("AmpLvl_LevelAdjustDecibels"),
    wxT("PitchLvl_DetuningPercentSemitones"),
    wxT("VoicingEQ01_HighFrequencyBoostDecibels"),
    wxT("VoicingEQ01_TransitionFrequencyKHertz"),
    wxT("HarmonicShaping_ThirdAndUpperHarmonicsLevelAdjustDecibels")};
  outFilter[WX_ATTACK]
    = {WX_LAYER_ID, WX_SAMPLE_ID, wxT("LoopCrossfadeLengthInSrcSampleMs")};
  outFilter[WX_RELEASE] = {
    WX_LAYER_ID,
    WX_SAMPLE_ID,
    wxT("ReleaseSelCriteria_LatestKeyReleaseTimeMs"),
    wxT("ReleaseCrossfadeLengthMs")};
  outFilter[WX_SAMPLE] = {
    WX_SAMPLE_ID,
    wxT("InstallationPackageID"),
    wxT("SampleFilename"),
    wxT("Pitch_ExactSamplePitch")};
}

GOHauptwerkToOdf::GOHauptwerkToOdf(
  const GOHauptwerkOdf &odf,
  const wxString &sampleSetPath,
  bool isVoicingEnabled,
  bool isWindModelEnabled,
  bool isSwitchesEnabled,
  bool isTremulantModelEnabled,
  bool isConsoleEnabled)
  : r_Odf(odf),
    m_IsVoicingEnabled(isVoicingEnabled),
    m_IsWindModelEnabled(isWindModelEnabled),
    m_IsSwitchesEnabled(isSwitchesEnabled),
    m_IsTremulantModelEnabled(isTremulantModelEnabled),
    m_IsConsoleEnabled(isConsoleEnabled),
    m_SampleSetPath(sampleSetPath),
    m_DrawstopCols(12),
    m_DrawstopRows(12),
    m_NPlacedDrawstops(0) {}

void GOHauptwerkToOdf::PlaceDrawstop(const wxString &group) {
  const unsigned nCells = m_DrawstopCols * m_DrawstopRows;
  const unsigned stopI = m_NPlacedDrawstops++;

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

wxString GOHauptwerkToOdf::ResolvePackagePath(
  const wxString &hwFileName, long installPackageId) const {
  wxString result;

  if (!hwFileName.IsEmpty()) {
    wxString relative = hwFileName;

    relative.Replace(wxT("\\"), wxT("/"));
    while (relative.StartsWith(wxT("/")))
      relative = relative.Mid(1);

    const wxString packageDir = wxString::Format(
      wxT("OrganInstallationPackages/%06ld"), installPackageId);
    const wxString full
      = m_SampleSetPath + wxT("/") + packageDir + wxT("/") + relative;

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

void GOHauptwerkToOdf::CheckInstallationPackages() {
  /* The definition names the packages it is played from. Saying which one is
   * missing beats letting the organ load without the sound it needs: every
   * sample would silently fail to resolve and the organ would come up mute. */
  for (const GOHauptwerkObject &package :
       r_Odf.GetObjects(WX_REQUIRED_INSTALLATION_PACKAGE)) {
    const long packageId = package.GetLong(WX_INSTALLATION_PACKAGE_ID);

    if (!wxFileName::DirExists(
          m_SampleSetPath + wxFileName::GetPathSeparator()
          + wxString::Format(
            wxT("OrganInstallationPackages/%06ld"), packageId)))
      Warn(wxString::Format(
        _("The organ is played from \"%s\" by %s, which is not installed: "
          "look for package %06ld under OrganInstallationPackages"),
        package.Get(WX_NAME),
        package.Get(wxT("SupplierName")),
        packageId));
  }
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

void GOHauptwerkToOdf::AnalyzeSwitches() {
  const std::vector<GOHauptwerkObject> &hwSwitches
    = r_Odf.GetObjects(WX_SWITCH);

  if (m_IsSwitchesEnabled && !hwSwitches.empty()) {
    std::unordered_map<long, const GOHauptwerkObject *> switchById;

    for (const GOHauptwerkObject &hwSwitch : hwSwitches)
      switchById[hwSwitch.GetLong(WX_SWITCH_ID)] = &hwSwitch;

    /* Only the links saying "the destination holds while the source holds"
     * are logic. The others set or clear the destination on a keypress, which
     * is a piston: what it does depends on the state Hauptwerk keeps between
     * presses, and a combinational switch has no such state. In this set they
     * are all one thing anyway - the master that loads the crescendo with its
     * defaults - and none of them reaches a stop. */
    std::unordered_map<long, std::vector<long>> sourcesById;

    for (const GOHauptwerkObject &link : r_Odf.GetObjects(WX_SWITCH_LINKAGE))
      if (
        link.GetLong(wxT("EngageLinkActionCode"), HW_LINK_ENGAGE)
          == HW_LINK_ENGAGE
        && link.GetLong(wxT("DisengageLinkActionCode"), HW_LINK_DISENGAGE)
          == HW_LINK_DISENGAGE)
        sourcesById[link.GetLong(wxT("DestSwitchID"))].push_back(
          link.GetLong(wxT("SourceSwitchID")));

    /* Only the switches something audible depends on. The list as a whole is
     * mostly console and combination bookkeeping - Nancy states eight
     * thousand of them against GrandOrgue's limit of 999 - while the part
     * that decides what sounds is a small fraction of it. */
    std::vector<long> pending;

    for (const GOHauptwerkObject &stop : r_Odf.GetObjects(WX_STOP))
      pending.push_back(stop.GetLong(WX_CONTROLLING_SWITCH_ID));
    for (const GOHauptwerkObject &tremulant : r_Odf.GetObjects(WX_TREMULANT))
      pending.push_back(tremulant.GetLong(WX_CONTROLLING_SWITCH_ID));
    for (const GOHauptwerkObject &keyAction : r_Odf.GetObjects(WX_KEY_ACTION))
      pending.push_back(keyAction.GetLong(wxT("ConditionSwitchID")));

    std::unordered_map<long, unsigned> nodeIByHwId;
    std::vector<long> hwIdByNodeI;

    while (!pending.empty()) {
      const long hwId = pending.back();

      pending.pop_back();
      if (
        hwId != 0 && switchById.find(hwId) != switchById.end()
        && nodeIByHwId.find(hwId) == nodeIByHwId.end()) {
        const auto sourcesIt = sourcesById.find(hwId);

        nodeIByHwId[hwId] = (unsigned)hwIdByNodeI.size();
        hwIdByNodeI.push_back(hwId);
        if (sourcesIt != sourcesById.end())
          for (long sourceId : sourcesIt->second)
            pending.push_back(sourceId);
      }
    }

    const unsigned nNodes = (unsigned)hwIdByNodeI.size();
    std::vector<std::vector<unsigned>> sourceNodeIs(nNodes);

    for (unsigned nodeI = 0; nodeI < nNodes; nodeI++) {
      const auto sourcesIt = sourcesById.find(hwIdByNodeI[nodeI]);

      if (sourcesIt != sourcesById.end())
        for (long sourceId : sourcesIt->second) {
          const auto sourceIt = nodeIByHwId.find(sourceId);

          if (sourceIt != nodeIByHwId.end())
            sourceNodeIs[nodeI].push_back(sourceIt->second);
        }
    }

    /* Tarjan, iteratively: the links run both ways between the switches that
     * stand for one control, so the graph has cycles, and each group of
     * mutually reachable switches becomes one GrandOrgue switch. An explicit
     * stack because the recursion would be as deep as the graph. */
    const unsigned UNVISITED = (unsigned)-1;
    std::vector<unsigned> visitOrder(nNodes, UNVISITED);
    std::vector<unsigned> lowLink(nNodes, 0);
    std::vector<bool> isOnStack(nNodes, false);
    std::vector<unsigned> componentByNodeI(nNodes, UNVISITED);
    std::vector<unsigned> componentStack;
    std::vector<std::pair<unsigned, unsigned>> walk;
    unsigned nVisited = 0;
    unsigned nComponents = 0;

    for (unsigned rootI = 0; rootI < nNodes; rootI++)
      if (visitOrder[rootI] == UNVISITED) {
        walk.push_back(std::make_pair(rootI, 0));
        while (!walk.empty()) {
          const unsigned nodeI = walk.back().first;
          const std::vector<unsigned> &nodeSources = sourceNodeIs[nodeI];
          unsigned edgeI = walk.back().second;
          bool isDescending = false;

          if (edgeI == 0) {
            visitOrder[nodeI] = lowLink[nodeI] = nVisited++;
            componentStack.push_back(nodeI);
            isOnStack[nodeI] = true;
          }
          while (!isDescending && edgeI < nodeSources.size()) {
            const unsigned nextI = nodeSources[edgeI++];

            if (visitOrder[nextI] == UNVISITED) {
              // Written back before the push: that can move the element away.
              walk.back().second = edgeI;
              walk.push_back(std::make_pair(nextI, 0));
              isDescending = true;
            } else if (isOnStack[nextI] && visitOrder[nextI] < lowLink[nodeI])
              lowLink[nodeI] = visitOrder[nextI];
          }
          if (!isDescending) {
            if (lowLink[nodeI] == visitOrder[nodeI]) {
              unsigned poppedI;

              do {
                poppedI = componentStack.back();
                componentStack.pop_back();
                isOnStack[poppedI] = false;
                componentByNodeI[poppedI] = nComponents;
              } while (poppedI != nodeI);
              nComponents++;
            }
            walk.pop_back();
            if (!walk.empty()) {
              const unsigned parentI = walk.back().first;

              if (lowLink[nodeI] < lowLink[parentI])
                lowLink[parentI] = lowLink[nodeI];
            }
          }
        }
      }

    std::vector<std::vector<unsigned>> nodeIsByComponent(nComponents);
    std::vector<std::set<unsigned>> sourceComponents(nComponents);

    for (unsigned nodeI = 0; nodeI < nNodes; nodeI++) {
      const unsigned componentI = componentByNodeI[nodeI];

      nodeIsByComponent[componentI].push_back(nodeI);
      for (unsigned sourceI : sourceNodeIs[nodeI])
        if (componentByNodeI[sourceI] != componentI)
          sourceComponents[componentI].insert(componentByNodeI[sourceI]);
    }

    /* GOOrganModel adds each switch to its list only after loading it, so a
     * switch naming a later one is out of range: they have to be emitted with
     * every source before the switch that reads it. */
    std::vector<unsigned> nPendingSources(nComponents, 0);
    std::vector<std::vector<unsigned>> dependents(nComponents);
    std::vector<unsigned> ready;
    std::vector<unsigned> orderedComponents;

    for (unsigned componentI = 0; componentI < nComponents; componentI++) {
      nPendingSources[componentI]
        = (unsigned)sourceComponents[componentI].size();
      for (unsigned sourceI : sourceComponents[componentI])
        dependents[sourceI].push_back(componentI);
    }
    for (unsigned componentI = 0; componentI < nComponents; componentI++)
      if (nPendingSources[componentI] == 0)
        ready.push_back(componentI);
    while (!ready.empty()) {
      const unsigned componentI = ready.back();

      ready.pop_back();
      orderedComponents.push_back(componentI);
      for (unsigned dependentI : dependents[componentI]) {
        nPendingSources[dependentI]--;
        if (nPendingSources[dependentI] == 0)
          ready.push_back(dependentI);
      }
    }

    if (orderedComponents.size() > MAX_ODF_SWITCHES)
      Warn(wxString::Format(
        _("The organ states %u switch groups, more than the %u GrandOrgue "
          "allows; its stops are drawn directly instead"),
        (unsigned)orderedComponents.size(),
        MAX_ODF_SWITCHES));
    else if (orderedComponents.size() < nComponents)
      /* Condensing every cycle should have left an order; refuse rather than
       * emit a switch naming one that was never written. */
      Warn(_("The switches of this organ could not be put in a usable order; "
             "its stops are drawn directly instead"));
    else {
      std::vector<unsigned> switchNByComponent(nComponents, 0);

      for (unsigned n = (unsigned)orderedComponents.size(), orderI = 0;
           orderI < n;
           orderI++)
        switchNByComponent[orderedComponents[orderI]] = orderI + 1;
      for (unsigned componentI : orderedComponents) {
        GOSwitchComponent component;

        component.isDefaultEngaged = false;
        component.isClickable = false;
        for (unsigned nodeI : nodeIsByComponent[componentI]) {
          const long hwId = hwIdByNodeI[nodeI];
          const GOHauptwerkObject &hwSwitch = *switchById[hwId];
          const wxString &name = hwSwitch.Get(WX_NAME);

          component.hwSwitchIds.push_back(hwId);
          if (hwSwitch.Get(wxT("DefaultToEngaged")) == WX_ODF_YES)
            component.isDefaultEngaged = true;
          if (hwSwitch.Get(wxT("Clickable")) == WX_ODF_YES)
            component.isClickable = true;
          /* Hauptwerk marks the switches standing for a drawing of a control,
           * rather than for the control itself, with a leading underscore,
           * and the plain name is the one worth showing. */
          if (
            !name.IsEmpty()
            && (component.name.IsEmpty()
                || (component.name.StartsWith(wxT("__"))
                    && !name.StartsWith(wxT("__")))))
            component.name = name;
        }
        if (component.name.IsEmpty())
          component.name = wxString::Format(
            wxT("Switch %u"), (unsigned)m_SwitchComponents.size() + 1);
        for (unsigned sourceI : sourceComponents[componentI])
          component.inputSwitchNs.push_back(switchNByComponent[sourceI]);
        for (long hwId : component.hwSwitchIds)
          m_SwitchNumberByHwId[hwId] = (unsigned)m_SwitchComponents.size() + 1;
        m_SwitchComponents.push_back(component);
      }
    }
  }
}

void GOHauptwerkToOdf::BuildCrescendo() {
  /* Hauptwerk states the crescendo as combinations over the switches, so
   * there is nothing to build without them. */
  if (!m_SwitchComponents.empty()) {
    std::map<long, const GOHauptwerkObject *> stageById;

    for (const GOHauptwerkObject &combination :
         r_Odf.GetObjects(WX_COMBINATION))
      if (
        combination.GetLong(wxT("CombinationTypeCode"))
        == HW_COMBINATION_CRESCENDO)
        stageById[combination.GetLong(WX_COMBINATION_ID)] = &combination;

    if (!stageById.empty()) {
      std::unordered_map<long, std::vector<const GOHauptwerkObject *>>
        elementsByCombinationId;
      std::vector<long> stageIds;

      for (const GOHauptwerkObject &element :
           r_Odf.GetObjects(WX_COMBINATION_ELEMENT))
        elementsByCombinationId[element.GetLong(WX_COMBINATION_ID)].push_back(
          &element);
      // Ordered by id, which is the order the stages run in
      for (const auto &pair : stageById)
        stageIds.push_back(pair.first);

      const unsigned nStages = (unsigned)stageIds.size();

      /* Hauptwerk states as many stages as the organ has controls - Nancy has
       * 73, each adding one more stop than the last - while GrandOrgue's
       * crescendo pedal has 32 positions, so the stages are sampled evenly
       * across them. The first position is the first stage, the last is the
       * last, and the shape of the crescendo is kept. */
      for (unsigned stepN = 1; stepN <= N_CRESCENDO_STEPS; stepN++) {
        const unsigned stageI = N_CRESCENDO_STEPS > 1
          ? (stepN - 1) * (nStages - 1) / (N_CRESCENDO_STEPS - 1)
          : 0;
        const wxString group
          = wxString::Format(wxT("SetterCrescendo1_%03u"), stepN);
        const auto elementsIt = elementsByCombinationId.find(stageIds[stageI]);
        std::set<unsigned> engagedSwitchNs;
        std::set<unsigned> releasedSwitchNs;

        if (elementsIt != elementsByCombinationId.end())
          for (const GOHauptwerkObject *pElement : elementsIt->second) {
            const auto switchIt = m_SwitchNumberByHwId.find(
              pElement->GetLong(wxT("ControlledSwitchID")));

            if (switchIt != m_SwitchNumberByHwId.end()) {
              if (
                pElement->Get(wxT("InitialStoredStateIsEngaged")) == WX_ODF_YES)
                engagedSwitchNs.insert(switchIt->second);
              else
                releasedSwitchNs.insert(switchIt->second);
            }
          }

        unsigned switchRefN = 0;

        /* A stage says of every control whether it is on or off, not only
         * which ones are on, and GrandOrgue reads a negative number as "off".
         * Stating both is what lets the pedal be moved back down, and what
         * keeps the crescendo from touching the stops the player drew by
         * hand: those switches are not named here at all. */
        for (unsigned switchN : engagedSwitchNs)
          Set(
            group,
            wxString::Format(wxT("SwitchNumber%03u"), ++switchRefN),
            (long)switchN);
        for (unsigned switchN : releasedSwitchNs)
          if (engagedSwitchNs.find(switchN) == engagedSwitchNs.end())
            Set(
              group,
              wxString::Format(wxT("SwitchNumber%03u"), ++switchRefN),
              -(long)switchN);
        /* Required whatever they hold: GrandOrgue takes the presence of
         * NumberOfStops as the sign that a combination is stated at all. */
        Set(group, wxT("NumberOfStops"), 0L);
        Set(group, wxT("NumberOfCouplers"), 0L);
        Set(group, wxT("NumberOfTremulants"), 0L);
        Set(group, wxT("NumberOfDivisionalCouplers"), 0L);
        Set(group, wxT("NumberOfSwitches"), (long)switchRefN);
      }
    }
  }
}

void GOHauptwerkToOdf::SetConsoleMetrics(
  const wxString &group,
  unsigned nCols,
  unsigned nRows,
  unsigned screenWidth,
  unsigned screenHeight,
  bool hasTrimAboveManuals) {
  Set(group, wxT("DispScreenSizeHoriz"), (long)screenWidth);
  Set(group, wxT("DispScreenSizeVert"), (long)screenHeight);
  Set(group, wxT("DispDrawstopCols"), (long)nCols);
  Set(group, wxT("DispDrawstopRows"), (long)nRows);
  Set(group, wxT("DispExtraDrawstopCols"), 6L);
  Set(group, wxT("DispExtraDrawstopRows"), 5L);
  Set(group, wxT("DispExtraDrawstopRowsAboveExtraButtonRows"), WX_ODF_YES);
  Set(group, wxT("DispButtonCols"), 10L);
  Set(group, wxT("DispExtraButtonRows"), 0L);
  Set(group, wxT("DispButtonsAboveManuals"), WX_ODF_NO);
  Set(group, wxT("DispDrawstopColsOffset"), WX_ODF_NO);
  Set(group, wxT("DispDrawstopOuterColOffsetUp"), WX_ODF_NO);
  Set(group, wxT("DispPairDrawstopCols"), WX_ODF_NO);
  Set(group, wxT("DispExtraPedalButtonRow"), WX_ODF_NO);
  Set(group, wxT("DispExtraPedalButtonRowOffset"), WX_ODF_NO);
  Set(group, wxT("DispExtraPedalButtonRowOffsetRight"), WX_ODF_NO);
  Set(
    group,
    wxT("DispTrimAboveManuals"),
    hasTrimAboveManuals ? WX_ODF_YES : WX_ODF_NO);
  Set(group, wxT("DispTrimBelowManuals"), WX_ODF_NO);
  Set(group, wxT("DispTrimAboveExtraRows"), WX_ODF_NO);
  Set(group, wxT("DispControlLabelFont"), wxT("Arial"));
  Set(group, wxT("DispGroupLabelFont"), wxT("Arial"));
  Set(group, wxT("DispShortcutKeyLabelFont"), wxT("Arial"));
  Set(group, wxT("DispShortcutKeyLabelColour"), wxT("Black"));
  Set(group, wxT("DispConsoleBackgroundImageNum"), 1L);
  Set(group, wxT("DispDrawstopBackgroundImageNum"), 1L);
  Set(group, wxT("DispDrawstopInsetBackgroundImageNum"), 1L);
  Set(group, wxT("DispKeyHorizBackgroundImageNum"), 1L);
  Set(group, wxT("DispKeyVertBackgroundImageNum"), 1L);
}

void GOHauptwerkToOdf::BuildPanels() {
  /* Hauptwerk's own console, drawn from its own artwork, as extra panels
   * beside the generic one this converter always lays out. Beside rather than
   * instead: a panel that cannot find its images is a panel with nothing to
   * click, and the generic console is what keeps the organ playable. */
  if (m_IsConsoleEnabled && !m_SwitchComponents.empty()) {
    std::unordered_map<long, const GOHauptwerkObject *> imageSetById;
    std::unordered_map<long, const GOHauptwerkObject *> instanceById;
    // image set -> index within the set -> the file holding that image
    std::unordered_map<long, std::map<long, wxString>> bitmapsBySetId;
    // Hauptwerk switch -> the switch number the component it fell into got
    std::unordered_map<long, unsigned> switchNByHwId = m_SwitchNumberByHwId;
    unsigned panelN = 0;

    for (const GOHauptwerkObject &imageSet : r_Odf.GetObjects(WX_IMAGE_SET))
      imageSetById[imageSet.GetLong(wxT("ImageSetID"))] = &imageSet;
    for (const GOHauptwerkObject &element :
         r_Odf.GetObjects(WX_IMAGE_SET_ELEMENT))
      bitmapsBySetId[element.GetLong(wxT("ImageSetID"))]
                    [element.GetLong(wxT("ImageIndexWithinSet"))]
        = element.Get(wxT("BitmapFilename"));
    for (const GOHauptwerkObject &instance :
         r_Odf.GetObjects(WX_IMAGE_SET_INSTANCE))
      instanceById[instance.GetLong(wxT("ImageSetInstanceID"))] = &instance;

    /* Which instance draws which switch, and in which of its images. One
     * drawstop is several switches in Hauptwerk - the stop, its picture on
     * the console, its picture on the jamb - and they were condensed into one
     * switch here, so the same switch is drawn on several pages, which is
     * what Hauptwerk does too. */
    std::unordered_map<long, const GOHauptwerkObject *> switchByInstanceId;

    for (const GOHauptwerkObject &hwSwitch : r_Odf.GetObjects(WX_SWITCH)) {
      const long instanceId = hwSwitch.GetLong(wxT("Disp_ImageSetInstanceID"));

      if (
        instanceId != 0
        && switchNByHwId.find(hwSwitch.GetLong(WX_SWITCH_ID))
          != switchNByHwId.end())
        switchByInstanceId[instanceId] = &hwSwitch;
    }

    for (const GOHauptwerkObject &page : r_Odf.GetObjects(WX_DISPLAY_PAGE)) {
      const long pageId = page.GetLong(wxT("PageID"));
      // Ordered so the panel comes out the same way every time it is built
      std::map<long, const GOHauptwerkObject *> backgroundsByOrder;
      std::map<unsigned, const GOHauptwerkObject *> instancesBySwitchN;
      unsigned screenWidth = 0;
      unsigned screenHeight = 0;
      unsigned instanceOrder = 0;

      for (const GOHauptwerkObject &instance :
           r_Odf.GetObjects(WX_IMAGE_SET_INSTANCE)) {
        instanceOrder++;
        if (instance.GetLong(wxT("DisplayPageID")) == pageId) {
          const auto imageSetIt
            = imageSetById.find(instance.GetLong(wxT("ImageSetID")));
          const auto switchIt = switchByInstanceId.find(
            instance.GetLong(wxT("ImageSetInstanceID")));

          if (imageSetIt != imageSetById.end()) {
            const unsigned right
              = (unsigned)(instance.GetLong(wxT("LeftXPosPixels"))
                           + imageSetIt->second->GetLong(
                             wxT("ImageWidthPixels")));
            const unsigned bottom
              = (unsigned)(instance.GetLong(wxT("TopYPosPixels"))
                           + imageSetIt->second->GetLong(
                             wxT("ImageHeightPixels")));

            if (right > screenWidth)
              screenWidth = right;
            if (bottom > screenHeight)
              screenHeight = bottom;
            if (switchIt != switchByInstanceId.end())
              instancesBySwitchN[switchNByHwId[switchIt->second->GetLong(
                WX_SWITCH_ID)]]
                = &instance;
            else
              /* Everything that is not a control is the picture of the
               * console. Keyed on the layer Hauptwerk gives it so the panel
               * is painted back to front, and on the order it was stated in
               * to keep two things on one layer apart. */
              backgroundsByOrder
                [instance.GetLong(wxT("ScreenLayerNumber")) * 100000
                 + instanceOrder]
                = &instance;
          }
        }
      }

      // A page with nothing to click is Hauptwerk's own settings or about
      // screen, which has no meaning here.
      if (!instancesBySwitchN.empty() && panelN < MAX_ODF_PANELS) {
        const wxString panelGroup = numbered(wxT("Panel"), ++panelN);
        unsigned imageN = 0;
        unsigned switchRefN = 0;

        Set(panelGroup, WX_NAME, page.Get(WX_NAME));
        Set(panelGroup, wxT("Group"), _("Hauptwerk console"));
        Set(panelGroup, wxT("HasPedals"), WX_ODF_NO);
        /* Nothing on this panel is laid out on the grid - every control
         * is placed at the pixel Hauptwerk put it at - but the grid is
         * still read, so it is given the smallest legal one. */
        SetConsoleMetrics(panelGroup, 2, 1, screenWidth, screenHeight, false);
        for (const wxString &key :
             {wxT("NumberOfEnclosures"),
              wxT("NumberOfTremulants"),
              wxT("NumberOfDivisionalCouplers"),
              wxT("NumberOfGenerals"),
              wxT("NumberOfReversiblePistons"),
              wxT("NumberOfManuals"),
              wxT("NumberOfCouplers"),
              wxT("NumberOfStops"),
              wxT("NumberOfDivisionals"),
              wxT("NumberOfLabels")})
          Set(panelGroup, key, 0L);

        for (const auto &pair : backgroundsByOrder) {
          const GOHauptwerkObject &instance = *pair.second;
          const GOHauptwerkObject &imageSet
            = *imageSetById[instance.GetLong(wxT("ImageSetID"))];
          const wxString path = ResolvePackagePath(
            bitmapsBySetId[instance.GetLong(wxT("ImageSetID"))]
                          [instance.GetLong(wxT("DefaultImageIndexWithinSet"))],
            imageSet.GetLong(WX_INSTALLATION_PACKAGE_ID));

          if (!path.IsEmpty()) {
            const wxString imageGroup
              = panelGroup + wxString::Format(wxT("Image%03u"), ++imageN);

            Set(imageGroup, wxT("Image"), path);
            Set(
              imageGroup,
              wxT("PositionX"),
              instance.GetLong(wxT("LeftXPosPixels")));
            Set(
              imageGroup,
              wxT("PositionY"),
              instance.GetLong(wxT("TopYPosPixels")));
          }
        }
        Set(panelGroup, wxT("NumberOfImages"), (long)imageN);

        for (const auto &pair : instancesBySwitchN) {
          const unsigned switchN = pair.first;
          const GOHauptwerkObject &instance = *pair.second;
          const GOHauptwerkObject &hwSwitch
            = *switchByInstanceId[instance.GetLong(wxT("ImageSetInstanceID"))];
          const long imageSetId = instance.GetLong(wxT("ImageSetID"));
          const GOHauptwerkObject &imageSet = *imageSetById[imageSetId];
          const wxString onPath = ResolvePackagePath(
            bitmapsBySetId[imageSetId]
                          [hwSwitch.GetLong(wxT("Disp_ImageSetIndexEngaged"))],
            imageSet.GetLong(WX_INSTALLATION_PACKAGE_ID));
          const wxString offPath = ResolvePackagePath(
            bitmapsBySetId[imageSetId][hwSwitch.GetLong(
              wxT("Disp_ImageSetIndexDisengaged"))],
            imageSet.GetLong(WX_INSTALLATION_PACKAGE_ID));

          if (!onPath.IsEmpty() && !offPath.IsEmpty()) {
            const wxString elementGroup
              = panelGroup + wxString::Format(wxT("Switch%03u"), switchN);

            Set(
              panelGroup,
              wxString::Format(wxT("Switch%03u"), ++switchRefN),
              (long)switchN);
            Set(elementGroup, wxT("ImageOn"), onPath);
            Set(elementGroup, wxT("ImageOff"), offPath);
            Set(
              elementGroup,
              wxT("PositionX"),
              instance.GetLong(wxT("LeftXPosPixels")));
            Set(
              elementGroup,
              wxT("PositionY"),
              instance.GetLong(wxT("TopYPosPixels")));
            /* The drawstop's name is painted into the artwork already, so
             * GrandOrgue must not write it over the top. */
            Set(elementGroup, wxT("TextBreakWidth"), 0L);
            SetMouseRect(elementGroup, imageSet);
          }
        }
        Set(panelGroup, wxT("NumberOfSwitches"), (long)switchRefN);
      }
    }
    Set(WX_ORGAN, wxT("NumberOfPanels"), (long)panelN);
  }
}

void GOHauptwerkToOdf::SetMouseRect(
  const wxString &group, const GOHauptwerkObject &imageSet) {
  /* Where the image responds to a click. Hauptwerk gives the edges relative
   * to the image, GrandOrgue an offset and a size. */
  const long left
    = imageSet.GetLong(wxT("ClickableAreaLeftRelativeXPosPixels"));
  const long top = imageSet.GetLong(wxT("ClickableAreaTopRelativeYPosPixels"));
  const long right
    = imageSet.GetLong(wxT("ClickableAreaRightRelativeXPosPixels"));
  const long bottom
    = imageSet.GetLong(wxT("ClickableAreaBottomRelativeYPosPixels"));

  if (right > left && bottom > top) {
    Set(group, wxT("MouseRectLeft"), left);
    Set(group, wxT("MouseRectTop"), top);
    Set(group, wxT("MouseRectWidth"), right - left);
    Set(group, wxT("MouseRectHeight"), bottom - top);
  }
}

void GOHauptwerkToOdf::BuildSwitches() {
  unsigned switchN = 0;

  for (const GOSwitchComponent &component : m_SwitchComponents) {
    const wxString group = numbered(wxT("Switch"), ++switchN);

    Set(group, WX_NAME, component.name);
    if (component.inputSwitchNs.empty()) {
      Set(
        group,
        wxT("DefaultToEngaged"),
        component.isDefaultEngaged ? WX_ODF_YES : WX_ODF_NO);
      /* Only what Hauptwerk says the player can operate is drawn. The rest is
       * the organ's internal wiring, and a console showing all of it would
       * bury the drawstops among hundreds of switches. */
      if (component.isClickable) {
        Set(group, wxT("Displayed"), WX_ODF_YES);
        PlaceDrawstop(group);
      } else
        Set(group, wxT("Displayed"), WX_ODF_NO);
    } else {
      /* Any source holding is enough to hold this one. A switch with a
       * function is read-only in GrandOrgue, which is right here: these
       * follow the switches the player operates. */
      unsigned inputN = 0;

      Set(group, wxT("Function"), wxT("Or"));
      Set(group, wxT("SwitchCount"), (long)component.inputSwitchNs.size());
      for (unsigned inputSwitchN : component.inputSwitchNs)
        Set(
          group,
          wxString::Format(wxT("Switch%03u"), ++inputN),
          (long)inputSwitchN);
      Set(group, wxT("Displayed"), WX_ODF_NO);
    }
  }
  Set(WX_ORGAN, wxT("NumberOfSwitches"), (long)switchN);
}

bool GOHauptwerkToOdf::ControlByHwSwitch(
  const wxString &group, long hwSwitchId) {
  const auto switchIt = m_SwitchNumberByHwId.find(hwSwitchId);
  const bool isFound = switchIt != m_SwitchNumberByHwId.end();

  if (isFound) {
    Set(group, wxT("Function"), wxT("Or"));
    Set(group, wxT("SwitchCount"), 1L);
    Set(group, wxT("Switch001"), (long)switchIt->second);
    // The switch is the drawstop the player sees; this one follows it.
    Set(group, wxT("Displayed"), WX_ODF_NO);
  }
  return isFound;
}

void GOHauptwerkToOdf::BuildWindchests() {
  /* What each compartment is fed, in kilograms of air per second - the same
   * units the pipes state their draw in, so the two can be compared directly
   * instead of through a scale factor. A compartment nothing feeds is a
   * source, the blower, and is left unlimited. */
  std::unordered_map<long, double> supplyKgPerSecById;
  unsigned windchestN = 0;

  for (const GOHauptwerkObject &linkage :
       r_Odf.GetObjects(WX_WIND_COMPARTMENT_LINKAGE))
    supplyKgPerSecById[linkage.GetLong(wxT("SecondWindCompartmentID"))]
      += wxAtof(
        linkage.Get(wxT("MassFlowRateKilogramsPerSecAtReferencePressureDiff")));

  for (const GOHauptwerkObject &compartment :
       r_Odf.GetObjects(WX_WIND_COMPARTMENT)) {
    const long id = compartment.GetLong(wxT("WindCompartmentID"));
    const wxString group = numbered(wxT("WindchestGroup"), ++windchestN);

    m_WindchestNumberById[id] = windchestN;
    Set(group, WX_NAME, compartment.Get(WX_NAME));

    if (m_IsWindModelEnabled && !compartment.IsYes(wxT("InfiniteVolume"))) {
      const auto supplyIt = supplyKgPerSecById.find(id);

      if (supplyIt != supplyKgPerSecById.end() && supplyIt->second > 0)
        Set(
          group,
          wxT("WindSupplyCapacity"),
          wxString::Format(wxT("%.6f"), supplyIt->second));
    }
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
  /* Which windchest each pipe sits on. Built here rather than where it is
   * first read: the tremulants and the enclosures both need it, and this is
   * the earliest point at which the windchests have their numbers. */
  for (const GOHauptwerkObject &pipe : r_Odf.GetObjects(WX_PIPE)) {
    const long compartmentId
      = pipe.GetLong(wxT("WindSupply_SourceWindCompartmentID"));
    const auto windchestIt = m_WindchestNumberById.find(compartmentId);

    m_WindchestNumberByPipeId[pipe.GetLong(WX_PIPE_ID)]
      = windchestIt != m_WindchestNumberById.end() ? windchestIt->second : 1;
  }
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
  /* The keys the player can actually reach, which is a different thing: the
   * compass above is the division's, and it runs past the keyboard wherever
   * the division is played by a coupler as well as by hands. Lowest note and
   * count, per division. */
  std::unordered_map<long, std::pair<long, long>> keyboardByDivisionId;
  std::unordered_map<long, long> divisionIdByKeyboardId;

  for (const GOHauptwerkObject &keyboard : r_Odf.GetObjects(WX_KEYBOARD)) {
    const long divisionId
      = keyboard.GetLong(wxT("Hint_PrimaryAssociatedDivisionID"), 0);

    if (divisionId != 0)
      divisionIdByKeyboardId[keyboard.GetLong(wxT("KeyboardID"))] = divisionId;
  }
  for (const GOHauptwerkObject &key : r_Odf.GetObjects(WX_KEYBOARD_KEY)) {
    const auto divisionIt
      = divisionIdByKeyboardId.find(key.GetLong(wxT("KeyboardID")));
    const long note = key.GetLong(wxT("NormalMIDINoteNumber"), -1);

    if (divisionIt != divisionIdByKeyboardId.end() && note >= 0) {
      auto &keyboardKeys = keyboardByDivisionId[divisionIt->second];

      if (keyboardKeys.second == 0 || note < keyboardKeys.first)
        keyboardKeys.first = note;
      keyboardKeys.second++;
    }
  }

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

    /* The extended range exists for couplers, not for fingers, so the player
     * gets the keyboard the set states and the rest stays reachable by
     * coupling. Only where it states none does the conventional compass -
     * 32 notes from C on a pedalboard, 61 on a manual - stand in for it. */
    const auto keyboardIt = keyboardByDivisionId.find(divisionId);
    const bool hasKeyboard = keyboardIt != keyboardByDivisionId.end()
      && keyboardIt->second.second > 0;
    const long firstAccessibleNote
      = hasKeyboard ? std::max(keyboardIt->second.first, firstNote) : firstNote;
    const long firstAccessibleKey = firstAccessibleNote - firstNote + 1;
    const long nAccessibleKeys = std::min(
      nLogicalKeys - firstAccessibleKey + 1,
      hasKeyboard ? keyboardIt->second.second : (isPedal ? 32L : 61L));

    m_ManualNumberByDivisionId[divisionId] = number;
    Set(group, WX_NAME, division.Get(WX_NAME));
    Set(group, wxT("NumberOfLogicalKeys"), nLogicalKeys);
    Set(group, wxT("FirstAccessibleKeyLogicalKeyNumber"), firstAccessibleKey);
    Set(group, wxT("FirstAccessibleKeyMIDINoteNumber"), firstAccessibleNote);
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
      pipes[0]->GetLong(wxT("WindSupply_SourceWindCompartmentID")));

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
    std::vector<GOHauptwerkRelease> releases;
    // Voicing lives on the layer, and only the first layer is used, so the
    // first one that states a value is the one that counts.
    double gainDb = 0.0;
    double detuneCents = 0.0;
    // The shelf the voicer applied: where it starts, and by how much
    double eqFrequencyHz = 0.0;
    double eqGainDb = 0.0;
    // The pitch the attack was recorded at, which is not the pitch the pipe
    // sounds wherever a rank is filled out by transposing another recording.
    double attackHz = 0.0;
    // How long the loop takes to fade back over itself
    long loopCrossfadeMs = 0;
    bool hasVoicing = false;

    if (layersIt != m_LayersByPipeId.end())
      for (const GOHauptwerkObject *pLayer : layersIt->second) {
        const long layerId = pLayer->GetLong(WX_LAYER_ID);

        if (!hasVoicing) {
          gainDb = wxAtof(pLayer->Get(wxT("AmpLvl_LevelAdjustDecibels")));
          // Percent of a semitone, and a semitone is a hundred cents, so the
          // number carries over unchanged.
          detuneCents
            = wxAtof(pLayer->Get(wxT("PitchLvl_DetuningPercentSemitones")));
          /* The voicer's own tone shaping: a lift or a drop above a
           * stated frequency. Hauptwerk states it twice over - once as a
           * plain shelf, once as an adjustment to the third and upper
           * harmonics - and the two land on the same shelf. */
          eqGainDb
            = wxAtof(pLayer->Get(wxT("VoicingEQ01_HighFrequencyBoostDecibels")))
            + wxAtof(pLayer->Get(wxT(
              "HarmonicShaping_ThirdAndUpperHarmonicsLevelAdjustDecibels")));
          eqFrequencyHz = 1000.0
            * wxAtof(pLayer->Get(wxT("VoicingEQ01_TransitionFrequencyKHertz")));
          hasVoicing = true;
        }

        const auto attacksIt = m_AttacksByLayerId.find(layerId);

        if (attacksIt != m_AttacksByLayerId.end())
          for (const GOHauptwerkObject *pAttack : attacksIt->second) {
            const GOHauptwerkObject *pSample = r_Odf.FindById(
              WX_SAMPLE, WX_SAMPLE_ID, pAttack->GetLong(WX_SAMPLE_ID));

            if (pSample && attackPath.IsEmpty()) {
              attackPath = ResolvePackagePath(
                pSample->Get(wxT("SampleFilename")),
                pSample->GetLong(wxT("InstallationPackageID")));
              attackHz = wxAtof(pSample->Get(wxT("Pitch_ExactSamplePitch")));
              loopCrossfadeMs
                = pAttack->GetLong(wxT("LoopCrossfadeLengthInSrcSampleMs"), 0);
            }
          }

        const auto releasesIt = m_ReleasesByLayerId.find(layerId);

        if (releasesIt != m_ReleasesByLayerId.end())
          for (const GOHauptwerkObject *pRelease : releasesIt->second) {
            const GOHauptwerkObject *pSample = r_Odf.FindById(
              WX_SAMPLE, WX_SAMPLE_ID, pRelease->GetLong(WX_SAMPLE_ID));

            if (pSample) {
              const wxString path = ResolvePackagePath(
                pSample->Get(wxT("SampleFilename")),
                pSample->GetLong(wxT("InstallationPackageID")));

              if (!path.IsEmpty())
                releases.push_back(
                  {path,
                   pRelease->GetLong(
                     wxT("ReleaseSelCriteria_LatestKeyReleaseTimeMs"), -1),
                   pRelease->GetLong(wxT("ReleaseCrossfadeLengthMs"), 0)});
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
      const long harmonic
        = pipe.GetLong(wxT("Pitch_Tempered_RankBasePitch64ftHarmonicNum"), 0);

      Set(group, pipeKey, attackPath);
      if (loopCrossfadeMs > 0 && loopCrossfadeMs <= MAX_CROSSFADE_MS)
        Set(group, pipeKey + wxT("LoopCrossfadeLength"), loopCrossfadeMs);
      if (harmonic > 0)
        Set(group, pipeKey + wxT("HarmonicNumber"), harmonic);

      if (m_IsWindModelEnabled) {
        const double flow = wxAtof(pipe.Get(wxT(
          "WindSupply_MassFlowRateKilogramsPerSecAtReferencePressureDiff")));

        if (flow > 0)
          Set(
            group,
            pipeKey + wxT("WindFlow"),
            wxString::Format(wxT("%.8f"), flow));
      }

      /* Tuning, which is not voicing and so is not switchable: getting it
       * wrong is not a matter of taste. Two different pitches are stated and
       * they have to be kept apart. The sample says what was recorded; the
       * pipe says what it sounded like in the organ it came from, and where a
       * rank was filled out by transposing a lower recording - the top of
       * Nancy's Trompette is its own octave below - the two are an octave
       * apart.
       *
       * All of it is carried in PitchTuning rather than in the key number.
       * Cents are continuous where a key number is not, so a set that states
       * a pipe slightly out of tune keeps that, and anything a voicer adds
       * later simply adds to the same number. The key number is left as a
       * fixed reference - the equally tempered pitch of the key, shifted by
       * the rank's harmonic number - which is the pitch GrandOrgue would play
       * the pipe at with no tuning at all. */
      const double soundedHz
        = wxAtof(pipe.Get(wxT("Pitch_OriginalOrgan_PitchHz")));
      const long noteN = firstMidiNote + (long)pipeI;
      double tuningCents = m_IsVoicingEnabled ? detuneCents : 0.0;

      if (harmonic > 0 && noteN >= 0 && noteN <= 127) {
        const double referenceHz
          = 440.0 * std::pow(2.0, (noteN - 69) / 12.0) * (double)harmonic / 8.0;
        const double midiExact = 69.0 + 12.0 * std::log2(referenceHz / 440.0);
        const double midiKey = std::floor(midiExact);

        if (midiKey >= 0 && midiKey <= 127) {
          Set(group, pipeKey + wxT("MIDIKeyNumber"), (long)midiKey);
          Set(
            group,
            pipeKey + wxT("MIDIPitchFraction"),
            wxString::Format(wxT("%.6f"), (midiExact - midiKey) * 100.0));
        }
      }
      if (attackHz > 8.0 && soundedHz > 8.0)
        tuningCents += 1200.0 * std::log2(soundedHz / attackHz);
      if (m_IsVoicingEnabled && gainDb != 0.0)
        Set(
          group, pipeKey + wxT("Gain"), wxString::Format(wxT("%.4f"), gainDb));
      /* Both halves have to be stated for a shelf to mean anything: a gain
       * with no frequency has nowhere to start. */
      if (m_IsVoicingEnabled && eqFrequencyHz > 0.0 && eqGainDb != 0.0) {
        Set(
          group,
          pipeKey + wxT("VoicingEQFrequency"),
          wxString::Format(wxT("%.2f"), eqFrequencyHz));
        Set(
          group,
          pipeKey + wxT("VoicingEQGain"),
          wxString::Format(wxT("%.4f"), eqGainDb));
      }
      if (tuningCents != 0.0)
        Set(
          group,
          pipeKey + wxT("PitchTuning"),
          wxString::Format(wxT("%.4f"), tuningCents));

      // Hauptwerk picks a release by how long the key was held, so the
      // shortest limit has to be tried first; the file does not list them in
      // that order. The longest one carries a sentinel rather than a real
      // limit (99999 ms), and GrandOrgue expects no MaxKeyPressTime at all
      // for the release that catches everything else.
      std::sort(
        releases.begin(),
        releases.end(),
        [](const GOHauptwerkRelease &a, const GOHauptwerkRelease &b) {
          return a.maxKeyPressMs < b.maxKeyPressMs;
        });

      Set(group, pipeKey + wxT("ReleaseCount"), (long)releases.size());
      for (unsigned nReleases = releases.size(), relI = 0; relI < nReleases;
           relI++) {
        const wxString relKey
          = wxString::Format(wxT("%sRelease%03u"), pipeKey, relI + 1);
        const GOHauptwerkRelease &release = releases[relI];

        Set(group, relKey, release.path);
        if (
          nReleases > 1 && release.maxKeyPressMs >= 0
          && release.maxKeyPressMs < HW_UNLIMITED_KEY_PRESS_MS)
          Set(group, relKey + wxT("MaxKeyPressTime"), release.maxKeyPressMs);
        /* How long the release takes to fade in over the tail of the attack.
         * Hauptwerk states it per release sample and GrandOrgue reads the
         * same thing in the same unit; leaving it out made every release
         * begin abruptly. */
        if (release.crossfadeMs > 0 && release.crossfadeMs <= MAX_CROSSFADE_MS)
          Set(
            group, relKey + wxT("ReleaseCrossfadeLength"), release.crossfadeMs);
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
      /* With a switch the drawstop the player sees is the switch, and this
       * follows it. Without one the stop has to be drawn itself, or there is
       * no way to engage it: GrandOrgue defaults Displayed to N. */
      if (!ControlByHwSwitch(group, stop.GetLong(WX_CONTROLLING_SWITCH_ID))) {
        Set(group, wxT("Displayed"), WX_ODF_YES);
        // GODrawstop reads this as a required value for a plain drawstop -
        // one with no Function - so it must be present even though N is what
        // a stop starts as anyway.
        Set(group, wxT("DefaultToEngaged"), WX_ODF_NO);
        PlaceDrawstop(group);
      }

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
  /* No Organ/NumberOfStops: GrandOrgue counts an organ's stops from the
   * manuals, and writing it only earns an unused-entry warning on every
   * load. */
}

void GOHauptwerkToOdf::MapKeyboardsToManuals() {
  for (const GOHauptwerkObject &keyboard : r_Odf.GetObjects(WX_KEYBOARD)) {
    const auto manualIt = m_ManualNumberByDivisionId.find(
      keyboard.GetLong(wxT("Hint_PrimaryAssociatedDivisionID")));

    if (manualIt != m_ManualNumberByDivisionId.end())
      m_ManualNumberByKeyboardId[keyboard.GetLong(wxT("KeyboardID"))]
        = manualIt->second;
  }

  /* The hint is not the whole map. Hauptwerk plays a division through a
   * keyboard that hints at no division at all - a bus - with the keyboard
   * under the player's hands feeding it and every coupler of that division
   * hanging off it. What says which division a bus belongs to is not a hint
   * but the wiring: each of its keys is a switch, each division input is a
   * switch, and a linkage joins the two. Reading only the hint drops every
   * action through the bus, which on the benchmark set is six couplers of
   * nine - the whole Grand Orgue jamb among them. */
  std::unordered_map<long, long> keyboardIdByKeySwitchId;
  std::unordered_map<long, long> divisionIdByInputSwitchId;

  for (const GOHauptwerkObject &key : r_Odf.GetObjects(WX_KEYBOARD_KEY))
    keyboardIdByKeySwitchId[key.GetLong(WX_SWITCH_ID)]
      = key.GetLong(wxT("KeyboardID"));
  for (const GOHauptwerkObject &input : r_Odf.GetObjects(WX_DIVISION_INPUT))
    divisionIdByInputSwitchId[input.GetLong(WX_SWITCH_ID)]
      = input.GetLong(WX_DIVISION_ID);

  // Keyboard -> every division its own keys are wired to
  std::unordered_map<long, std::set<long>> divisionIdsByKeyboardId;

  for (const GOHauptwerkObject &linkage : r_Odf.GetObjects(WX_SWITCH_LINKAGE)) {
    const auto keyboardIt
      = keyboardIdByKeySwitchId.find(linkage.GetLong(WX_SOURCE_SWITCH_ID));

    if (keyboardIt != keyboardIdByKeySwitchId.end()) {
      const auto divisionIt
        = divisionIdByInputSwitchId.find(linkage.GetLong(WX_DEST_SWITCH_ID));

      if (divisionIt != divisionIdByInputSwitchId.end())
        divisionIdsByKeyboardId[keyboardIt->second].insert(divisionIt->second);
    }
  }

  for (const auto &pair : divisionIdsByKeyboardId)
    /* Only where the hint left the keyboard unplaced, and only where the
     * wiring is unambiguous: keys reaching two divisions are a coupler
     * stated as wiring, not one manual's input. */
    if (
      pair.second.size() == 1
      && m_ManualNumberByKeyboardId.find(pair.first)
        == m_ManualNumberByKeyboardId.end()) {
      const auto manualIt
        = m_ManualNumberByDivisionId.find(*pair.second.begin());

      if (manualIt != m_ManualNumberByDivisionId.end())
        m_ManualNumberByKeyboardId[pair.first] = manualIt->second;
    }
}

void GOHauptwerkToOdf::BuildCouplers() {
  // Hauptwerk states a key action between keyboards; GrandOrgue states a
  // coupler on the source manual naming the destination one.
  MapKeyboardsToManuals();

  std::unordered_map<unsigned, unsigned> couplerCountByManual;
  unsigned couplerN = 0;

  for (const GOHauptwerkObject &action : r_Odf.GetObjects(WX_KEY_ACTION)) {
    const auto srcIt = m_ManualNumberByKeyboardId.find(
      action.GetLong(wxT("SourceKeyboardID")));
    const auto dstIt
      = m_ManualNumberByKeyboardId.find(action.GetLong(wxT("DestKeyboardID")));

    // A key action with an end that reaches no division couples nothing.
    if (
      srcIt != m_ManualNumberByKeyboardId.end()
      && dstIt != m_ManualNumberByKeyboardId.end()) {
      const long keyshift = action.GetLong(wxT("MIDINoteNumberIncrement"), 0);

      /* Source and destination on one manual is how an octave coupler is
       * stated - and also how the keyboard under the hands is joined to the
       * bus that plays its division. The shift tells the two apart: without
       * one they are the same keys, wiring already accounted for, and
       * emitting it would double every note the manual sounds. */
      if (srcIt->second == dstIt->second && keyshift == 0)
        continue;
      /* GOCoupler reads the shift as a required value within its own bounds
       * and refuses one outside them, taking the whole organ down over a
       * single coupler. */
      if (keyshift < MIN_KEYSHIFT || keyshift > MAX_KEYSHIFT) {
        Warn(wxString::Format(
          _("Coupler \"%s\" transposes by %ld semitones, more than "
            "GrandOrgue couples; it is left out"),
          action.Get(WX_NAME),
          keyshift));
        continue;
      }

      const wxString group = numbered(wxT("Coupler"), ++couplerN);

      Set(group, WX_NAME, action.Get(WX_NAME));
      Set(group, wxT("UnisonOff"), WX_ODF_NO);
      Set(group, wxT("DestinationManual"), (long)dstIt->second);
      Set(group, wxT("DestinationKeyshift"), keyshift);
      /* The keys the action actually carries. A tirasse is 32 notes wide
       * where the manual it couples to is 61, and letting that default would
       * couple keys the pedalboard has not got. */
      Set(
        group,
        wxT("FirstMIDINoteNumber"),
        action.GetLong(wxT("MIDINoteNumOfFirstSourceKey"), 0));
      Set(group, wxT("NumberOfKeys"), action.GetLong(wxT("NumberOfKeys"), 127));
      /* A Hauptwerk key action delivers its notes to a keyboard, and that
       * keyboard sends on everything it receives through its own outgoing
       * actions - there is no mark on an action saying whether what arrives
       * by coupling travels further, because it always does. So the couplers
       * cascade, which is also what the organ they are copied from does:
       * draw the tirasse and the Positif to Grand Orgue together on a French
       * console and the pedal sounds the Positif. */
      Set(
        group, wxT("CoupleToSubsequentUnisonIntermanualCouplers"), WX_ODF_YES);
      Set(
        group, wxT("CoupleToSubsequentUpwardIntermanualCouplers"), WX_ODF_YES);
      Set(
        group,
        wxT("CoupleToSubsequentDownwardIntermanualCouplers"),
        WX_ODF_YES);
      Set(
        group, wxT("CoupleToSubsequentUpwardIntramanualCouplers"), WX_ODF_YES);
      Set(
        group,
        wxT("CoupleToSubsequentDownwardIntramanualCouplers"),
        WX_ODF_YES);
      if (!ControlByHwSwitch(group, action.GetLong(wxT("ConditionSwitchID")))) {
        Set(group, wxT("Displayed"), WX_ODF_YES);
        Set(group, wxT("DefaultToEngaged"), WX_ODF_NO);
        PlaceDrawstop(group);
      }

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

unsigned GOHauptwerkToOdf::GetTremulantDepth(
  double totalAdjustDb, unsigned nPipes) const {
  /* Hauptwerk states, per pipe, how far the tremulant pulls that pipe's
   * amplitude down at the bottom of its swing, in decibels below the pipe's
   * own level. GrandOrgue wants the same thing as a percentage of the level,
   * which is what the decibels convert to: this organ's -19.2 dB is a swing
   * down to about a ninth of full, an ordinary tremulant depth. */
  unsigned depth = DEFAULT_TREMULANT_DEPTH;

  if (m_IsTremulantModelEnabled && nPipes > 0) {
    const double asFraction = pow(10.0, totalAdjustDb / nPipes / 20.0);
    // GOTremulant reads AmpModDepth as a percentage between 1 and 100
    const double asPercent = 100.0 * asFraction;

    depth
      = asPercent < 1.0 ? 1 : (asPercent > 100.0 ? 100 : (unsigned)asPercent);
  }
  return depth;
}

void GOHauptwerkToOdf::BuildTremulants() {
  /* Which pipes a tremulant acts on, and how hard, is stated per pipe rather
   * than on the tremulant: a waveform belongs to a tremulant, and every pipe
   * the tremulant reaches names that waveform along with its own depth. */
  std::unordered_map<long, long> tremulantIdByWaveformId;
  std::unordered_map<long, double> totalDepthDbByTremulantId;
  std::unordered_map<long, double> totalPitchPctByTremulantId;
  std::unordered_map<long, unsigned> nPipesByTremulantId;
  std::unordered_map<long, std::set<unsigned>> windchestNsByTremulantId;
  unsigned tremulantN = 0;

  for (const GOHauptwerkObject &waveform :
       r_Odf.GetObjects(WX_TREMULANT_WAVEFORM))
    tremulantIdByWaveformId[waveform.GetLong(wxT("TremulantWaveformID"))]
      = waveform.GetLong(wxT("TremulantID"));
  for (const GOHauptwerkObject &waveformPipe :
       r_Odf.GetObjects(WX_TREMULANT_WAVEFORM_PIPE)) {
    const auto tremulantIt = tremulantIdByWaveformId.find(
      waveformPipe.GetLong(wxT("TremulantWaveformID")));

    if (tremulantIt != tremulantIdByWaveformId.end()) {
      const long tremulantId = tremulantIt->second;
      const auto windchestIt
        = m_WindchestNumberByPipeId.find(waveformPipe.GetLong(WX_PIPE_ID));

      totalDepthDbByTremulantId[tremulantId]
        += wxAtof(waveformPipe.Get(wxT("AmplitudeModDepthAdjustDecibels")));
      totalPitchPctByTremulantId[tremulantId]
        += wxAtof(waveformPipe.Get(wxT("PitchModDepthAdjustPercent")));
      nPipesByTremulantId[tremulantId]++;
      if (windchestIt != m_WindchestNumberByPipeId.end())
        windchestNsByTremulantId[tremulantId].insert(windchestIt->second);
    }
  }

  for (const GOHauptwerkObject &tremulant : r_Odf.GetObjects(WX_TREMULANT)) {
    const wxString group = numbered(wxT("Tremulant"), ++tremulantN);
    const long tremulantId = tremulant.GetLong(wxT("TremulantID"));
    const double freqHz = wxAtof(tremulant.Get(wxT("FrequencyWhenEngagedHz")));
    // Hauptwerk gives a frequency, GrandOrgue a period in milliseconds.
    const long periodMs = freqHz > 0.1 ? (long)(1000.0 / freqHz) : 200L;

    m_TremulantNumberById[tremulantId] = tremulantN;
    Set(group, WX_NAME, tremulant.Get(WX_NAME));
    Set(group, wxT("Period"), periodMs);
    Set(
      group,
      wxT("AmpModDepth"),
      (long)GetTremulantDepth(
        totalDepthDbByTremulantId[tremulantId],
        nPipesByTremulantId[tremulantId]));

    /* How far the tremulant pulls the pitch, which Hauptwerk states per pipe
     * as a percentage of the frequency. A tremulant that only changes the
     * loudness sounds like a volume knob; the waver in pitch is most of what
     * makes it sound like wind. */
    const unsigned nTremulantPipes = nPipesByTremulantId[tremulantId];

    if (m_IsTremulantModelEnabled && nTremulantPipes > 0) {
      const double pitchPercent
        = totalPitchPctByTremulantId[tremulantId] / nTremulantPipes;

      /* Stated in cents rather than as the percentage Hauptwerk uses:
       * a percent of the frequency is 17 cents here, so rounding to whole
       * percent would throw away most of the difference between one pipe's
       * tremulant and another's. */
      const double pitchCents = 1200.0 * std::log2(1.0 + pitchPercent / 100.0);

      if (pitchCents >= 1.0)
        Set(
          group,
          wxT("PitchModDepth"),
          (long)(pitchCents < 1200.0 ? pitchCents : 1200.0));
    }
    Set(
      group, wxT("StartRate"), tremulant.GetLong(wxT("StartRatePercent"), 30));
    Set(group, wxT("StopRate"), tremulant.GetLong(wxT("StopRatePercent"), 30));
    if (!ControlByHwSwitch(
          group, tremulant.GetLong(WX_CONTROLLING_SWITCH_ID))) {
      Set(group, wxT("Displayed"), WX_ODF_YES);
      Set(group, wxT("DefaultToEngaged"), WX_ODF_NO);
      PlaceDrawstop(group);
    }
    const auto windchestNsIt = windchestNsByTremulantId.find(tremulantId);

    /* Only the windchests holding pipes this tremulant reaches. Nancy's one
     * tremulant is the Recit's and touches 976 of its 5196 pipes, so putting
     * it on every windchest would shake the whole organ. Falling back to that
     * only when the set names no pipes at all. */
    if (windchestNsIt != windchestNsByTremulantId.end())
      m_WindchestsByTremulantN[tremulantN] = windchestNsIt->second;
    else
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
  for (const GOHauptwerkObject &ep : r_Odf.GetObjects(WX_ENCLOSURE_PIPE)) {
    const auto encIt
      = m_EnclosureNumberById.find(ep.GetLong(wxT("EnclosureID")));
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
  // grid is sized here. This is GrandOrgue's own generic layout, sized to
  // fit; Hauptwerk's own console is drawn by BuildPanels, beside it.
  const unsigned nCols = 12;

  // Two drawstop columns flank each side of the manuals, so the grid holds
  // roughly nCols * nRows; round the rows up and leave a margin.
  unsigned nRows = (nStops + nCols - 1) / nCols + 2;

  if (nRows < 8)
    nRows = 8;
  if (nRows > 20)
    nRows = 20;
  m_DrawstopCols = nCols;
  m_DrawstopRows = nRows;
  SetConsoleMetrics(WX_ORGAN, nCols, nRows, 1900, 980, nManuals > 1);
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
  CheckInstallationPackages();
  BuildOrgan();
  BuildWindchests();
  BuildManuals();
  BuildRanks();
  AnalyzeSwitches();

  /* What the console has to hold: with switches it draws those the player
   * operates, and the stops follow them without being drawn themselves. */
  unsigned nDrawn = 0;

  for (const GOSwitchComponent &component : m_SwitchComponents)
    if (component.isClickable && component.inputSwitchNs.empty())
      nDrawn++;
  if (nDrawn == 0)
    nDrawn = r_Odf.GetObjectCount(WX_STOP);
  BuildDefaultConsole(nDrawn, r_Odf.GetObjectCount(WX_DIVISION));
  BuildSwitches();
  BuildCrescendo();
  BuildStops();
  BuildCouplers();
  BuildTremulants();
  BuildEnclosures();
  BuildPanels();
}
