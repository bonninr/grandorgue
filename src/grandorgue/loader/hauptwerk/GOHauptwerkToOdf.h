/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOHAUPTWERKTOODF_H
#define GOHAUPTWERKTOODF_H

#include <wx/string.h>

#include <map>
#include <set>
#include <unordered_map>
#include <vector>

class GOHauptwerkObject;
class GOHauptwerkOdf;

/**
 * Turns a parsed Hauptwerk definition into the settings a GrandOrgue ODF
 * would have provided.
 *
 * The output is the same group -> key -> value shape GOConfigFileReader
 * produces, so the whole existing load path runs unchanged afterwards and
 * nothing downstream needs to know an organ came from Hauptwerk. That is the
 * reason for converting to settings rather than building model objects
 * directly: the alternative would mean duplicating, in a second place, every
 * rule GOOrganModel already applies while reading an ODF.
 *
 * Scope, deliberately: this builds what makes an organ sound - ranks and
 * their pipes, stops, manuals, windchests, tremulants and enclosures. It does
 * not reproduce the Hauptwerk console graphics; GrandOrgue lays out a default
 * panel when an ODF declares none, which is enough to play with and avoids
 * carrying an image pipeline into the loader.
 */
class GOHauptwerkToOdf {
public:
  using GOOdfEntries = std::map<wxString, std::map<wxString, wxString>>;

private:
  const GOHauptwerkOdf &r_Odf;
  // Whether to carry Hauptwerk's per-pipe voicing across. Costs nothing at
  // play time - it all resolves during the load - but it changes the sound,
  // so it stays switchable like the rest.
  bool m_IsVoicingEnabled;
  /* Whether to state the wind supply. Unlike voicing this one costs at play
   * time, so it defaults off and stays independently switchable. */
  bool m_IsWindModelEnabled;
  // Folder holding OrganDefinitions and OrganInstallationPackages
  wxString m_SampleSetPath;
  GOOdfEntries m_Entries;
  std::vector<wxString> m_Warnings;
  // Console grid, decided before the stops are built so each one can be given
  // a cell: GrandOrgue defaults an unplaced drawstop to row 1 column 1, which
  // would stack every stop of the organ on the same spot.
  unsigned m_DrawstopCols;
  unsigned m_DrawstopRows;
  // Cells handed out so far, so switches and stops never land on the same one.
  unsigned m_NPlacedDrawstops;

  /**
   * One GrandOrgue switch, condensed from the Hauptwerk switches that behave
   * as a single control.
   *
   * Hauptwerk states one console control as several switches linked to each
   * other - the logical drawstop, its image on the console, its image on the
   * alternative jamb - and those links run both ways, so the graph has cycles
   * that GrandOrgue's combinational switches cannot express. Each group of
   * mutually reachable switches is therefore one switch here, which is also
   * what the group means: one thing the player operates.
   */
  struct GOSwitchComponent {
    wxString name;
    // Every switch of the group, so a consumer naming any of them finds it
    std::vector<long> hwSwitchIds;
    // Numbers of the switches feeding this one; empty makes it an input
    std::vector<unsigned> inputSwitchNs;
    bool isDefaultEngaged;
    // Hauptwerk says the player can operate it, so it is worth drawing
    bool isClickable;
  };

  // In an order where a switch only ever names earlier ones: GOOrganModel
  // appends each switch to its list after loading it, so a reference forward
  // is out of range.
  std::vector<GOSwitchComponent> m_SwitchComponents;
  // Hauptwerk switch id -> the one-based number of the switch holding it
  std::unordered_map<long, unsigned> m_SwitchNumberByHwId;

  // Hauptwerk id -> the one-based number the ODF group uses
  std::unordered_map<long, unsigned> m_ManualNumberByDivisionId;
  std::unordered_map<long, unsigned> m_WindchestNumberById;
  std::unordered_map<long, unsigned> m_RankNumberById;
  std::unordered_map<long, unsigned> m_TremulantNumberById;
  std::unordered_map<long, unsigned> m_EnclosureNumberById;
  // Hauptwerk hangs key actions off keyboards but stops off divisions, and
  // the two are related only by a hint on the keyboard.
  std::unordered_map<long, unsigned> m_ManualNumberByKeyboardId;
  // Windchests reached by an enclosure or a tremulant, by its ODF number.
  std::map<unsigned, std::set<unsigned>> m_WindchestsByEnclosureN;
  std::map<unsigned, std::set<unsigned>> m_WindchestsByTremulantN;
  std::unordered_map<long, unsigned> m_WindchestNumberByPipeId;

  // Layers, attacks and releases keyed by the object they hang off, so the
  // rank builder does not rescan tens of thousands of objects per pipe.
  std::unordered_map<long, std::vector<const GOHauptwerkObject *>>
    m_LayersByPipeId;
  std::unordered_map<long, std::vector<const GOHauptwerkObject *>>
    m_AttacksByLayerId;
  std::unordered_map<long, std::vector<const GOHauptwerkObject *>>
    m_ReleasesByLayerId;
  std::unordered_map<long, std::vector<const GOHauptwerkObject *>>
    m_PipesByRankId;

  void Set(const wxString &group, const wxString &key, const wxString &value);
  void Set(const wxString &group, const wxString &key, long value);
  void Warn(const wxString &message);

  void BuildIndexes();
  void BuildOrgan();
  /**
   * Works out the switches to emit, without emitting them: the console has to
   * be sized around how many of them are drawn, and that is only known once
   * the graph has been condensed.
   */
  void AnalyzeSwitches();
  /** Writes the switch sections AnalyzeSwitches worked out. */
  void BuildSwitches();
  void BuildWindchests();
  void BuildManuals();
  void BuildRanks();
  void BuildStops();
  void BuildCouplers();
  void BuildTremulants();
  void BuildEnclosures();
  void BuildRank(const GOHauptwerkObject &rank, unsigned rankN);
  /**
   * Size GrandOrgue's own console. It is drawn whatever NumberOfPanels says,
   * but from settings whose defaults assume a small organ, so a large set
   * needs the grid sized explicitly or the stops have nowhere to go. This is
   * not the Hauptwerk console: those graphics are not reproduced.
   */
  void BuildDefaultConsole(unsigned nStops, unsigned nManuals);
  /** Places one drawstop in the next free console cell. */
  void PlaceDrawstop(const wxString &group);
  /**
   * Points a drawstop at the switch that operates it, which makes it follow
   * that switch instead of being operated directly.
   *
   * @return whether a switch was found, leaving the drawstop unchanged if not
   */
  bool ControlByHwSwitch(const wxString &group, long hwSwitchId);

  /**
   * @return the sample path relative to the OrganDefinitions folder, in the
   *   form GrandOrgue expects, or an empty string when the file is missing
   */
  wxString ResolveSamplePath(
    const wxString &hwFileName, long installPackageId) const;

public:
  /**
   * @param odf a definition already read by GOHauptwerkOdf::Read
   * @param sampleSetPath the folder containing OrganDefinitions and
   *   OrganInstallationPackages - paths in the file are relative to it
   */
  GOHauptwerkToOdf(
    const GOHauptwerkOdf &odf,
    const wxString &sampleSetPath,
    bool isVoicingEnabled = true,
    bool isWindModelEnabled = false);

  /** Runs the conversion. Safe to call once per instance. */
  void Build();

  const GOOdfEntries &GetEntries() const { return m_Entries; }
  /** Non-fatal problems worth telling the user about, e.g. missing samples. */
  const std::vector<wxString> &GetWarnings() const { return m_Warnings; }

  /** The attribute filter GOHauptwerkOdf needs for this converter to work. */
  static void FillReadFilter(std::map<wxString, std::set<wxString>> &outFilter);
};

#endif /* GOHAUPTWERKTOODF_H */
