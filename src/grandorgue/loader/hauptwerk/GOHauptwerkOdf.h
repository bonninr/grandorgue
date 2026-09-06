/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOHAUPTWERKODF_H
#define GOHAUPTWERKODF_H

#include <wx/string.h>

#include <map>
#include <set>
#include <unordered_map>
#include <vector>

class GOOpenedFile;

/**
 * One object read out of a Hauptwerk organ definition, as a plain bag of
 * attributes. The format carries no type information beyond the object type
 * name, so values stay as strings and are converted where they are used.
 */
class GOHauptwerkObject {
private:
  std::map<wxString, wxString> m_Attributes;

public:
  /** @return the raw attribute value, or an empty string when absent. */
  const wxString &Get(const wxString &name) const;
  /** @return whether an attribute is present and not empty. */
  bool Has(const wxString &name) const;
  /**
   * @param defaultValue returned when the attribute is missing or does not
   *   parse as a number
   */
  long GetLong(const wxString &name, long defaultValue = 0) const;
  /** Hauptwerk spells booleans "Y" and "N". */
  bool IsYes(const wxString &name, bool isDefault = false) const;

  void Set(const wxString &name, const wxString &value) {
    m_Attributes[name] = value;
  }
  const std::map<wxString, wxString> &GetAttributes() const {
    return m_Attributes;
  }
};

/**
 * Reads a Hauptwerk organ definition (.Organ_Hauptwerk_xml) into memory.
 *
 * The file is a flat list of typed objects that reference each other by id:
 *
 *     <Hauptwerk FileFormat="Organ" FileFormatVersion="...">
 *       <ObjectList ObjectType="Rank">
 *         <Rank><RankID>1</RankID><Name>Montre 8</Name>...</Rank>
 *         ...
 *
 * Two things drive the design here. First, these files are large - a
 * three-manual set runs to 86 MB and 130,000 objects - so a DOM would cost
 * several hundred megabytes of nodes before any organ data exists, which is
 * not affordable on the small machines this is most useful on. The reader
 * therefore scans the file once and keeps only the object types and
 * attributes it was asked for; everything else is skipped without being
 * allocated.
 *
 * Second, Hauptwerk also has a "compressed" spelling of the same format in
 * which element names are one or two letters and need an external dictionary
 * to decode. That is not supported yet: IsCompressedFormat() reports it so a
 * caller can fail with an explanation rather than silently reading nothing.
 */
class GOHauptwerkOdf {
public:
  /**
   * Which attributes to retain for one object type.
   *
   * Ordered rather than hashed containers throughout this class: wxWidgets
   * only specialises std::hash<wxString> from 3.1, and GrandOrgue still
   * supports building against 3.0, where an unordered container keyed by
   * wxString does not compile.
   */
  using GOAttributeFilter = std::map<wxString, std::set<wxString>>;

private:
  wxString m_FileFormatVersion;
  wxString m_Hash;
  bool m_IsCompressedFormat;
  GOAttributeFilter m_Filter;

  // objects by type, in file order
  std::map<wxString, std::vector<GOHauptwerkObject>> m_ObjectsByType;

  // (type + "\n" + idAttribute) -> id -> index into m_ObjectsByType[type].
  // Built on first lookup rather than while parsing: the attribute holding
  // the id is named per type (RankID, StopID, PipeID, ...) with no rule that
  // derives it from the type name, so the reader would otherwise have to be
  // told all of them up front just in case.
  mutable std::map<wxString, std::unordered_map<long, unsigned>> m_IndexById;

  bool IsWanted(const wxString &objectType) const;
  bool IsWanted(const wxString &objectType, const wxString &attribute) const;
  void EnsureIndexed(
    const wxString &objectType, const wxString &idAttribute) const;
  void ParseBuffer(const wxString &buffer);

public:
  GOHauptwerkOdf();

  /**
   * Restrict what is read. Without a filter every attribute of every object
   * is kept, which is only reasonable for small files or for tooling that
   * genuinely wants everything.
   * @param filter object type -> attribute names to keep. An empty attribute
   *   set for a type keeps all of that type's attributes.
   */
  void SetFilter(const GOAttributeFilter &filter) { m_Filter = filter; }

  /**
   * Read and parse the definition.
   * @return an empty string on success, otherwise a message naming what was
   *   wrong with the file
   */
  wxString Read(GOOpenedFile *pFile);

  const wxString &GetFileFormatVersion() const { return m_FileFormatVersion; }
  /** Hash of the raw file, so the cache is keyed to this definition. */
  const wxString &GetHash() const { return m_Hash; }
  /** @return whether the file uses the letter-coded spelling we cannot read. */
  bool IsCompressedFormat() const { return m_IsCompressedFormat; }

  /** @return every object of a type, in the order the file listed them. */
  const std::vector<GOHauptwerkObject> &GetObjects(
    const wxString &objectType) const;
  unsigned GetObjectCount(const wxString &objectType) const;
  /**
   * @param idAttribute the attribute holding the id, e.g. "RankID". It must
   *   have been retained by the filter.
   * @return the matching object, or nullptr when there is none
   */
  const GOHauptwerkObject *FindById(
    const wxString &objectType, const wxString &idAttribute, long id) const;
};

#endif /* GOHAUPTWERKODF_H */
