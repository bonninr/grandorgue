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
  // Folder holding OrganDefinitions and OrganInstallationPackages
  wxString m_SampleSetPath;
  GOOdfEntries m_Entries;
  std::vector<wxString> m_Warnings;

  // Hauptwerk id -> the one-based number the ODF group uses
  std::unordered_map<long, unsigned> m_ManualNumberByDivisionId;
  std::unordered_map<long, unsigned> m_WindchestNumberById;
  std::unordered_map<long, unsigned> m_RankNumberById;
  std::unordered_map<long, unsigned> m_TremulantNumberById;
  std::unordered_map<long, unsigned> m_EnclosureNumberById;

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
  void BuildWindchests();
  void BuildManuals();
  void BuildRanks();
  void BuildStops();
  void BuildRank(const GOHauptwerkObject &rank, unsigned rankN);

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
  GOHauptwerkToOdf(const GOHauptwerkOdf &odf, const wxString &sampleSetPath);

  /** Runs the conversion. Safe to call once per instance. */
  void Build();

  const GOOdfEntries &GetEntries() const { return m_Entries; }
  /** Non-fatal problems worth telling the user about, e.g. missing samples. */
  const std::vector<wxString> &GetWarnings() const { return m_Warnings; }

  /** The attribute filter GOHauptwerkOdf needs for this converter to work. */
  static void FillReadFilter(std::map<wxString, std::set<wxString>> &outFilter);
};

#endif /* GOHAUPTWERKTOODF_H */
