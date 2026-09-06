/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2023 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOCONFIGFILEREADER_H
#define GOCONFIGFILEREADER_H

#include <wx/string.h>

#include <map>

class GOOpenedFile;

class GOConfigFileReader {
private:
  std::map<wxString, std::map<wxString, wxString>> m_Entries;
  wxString m_Hash;

  wxString GetNextLine(const wxString &buffer, unsigned &pos);

public:
  GOConfigFileReader();
  ~GOConfigFileReader();

  bool Read(GOOpenedFile *file);
  bool Read(wxString filename);
  /**
   * Take already-parsed settings instead of reading a file, for loaders that
   * build ODF content in memory - reading a foreign organ format, say. The
   * hash identifies the source for cache naming, exactly as the file hash
   * does for a real ODF.
   */
  void SetContent(
    const std::map<wxString, std::map<wxString, wxString>> &entries,
    const wxString &hash);
  wxString GetHash();

  const std::map<wxString, std::map<wxString, wxString>> &GetContent();
  wxString getEntry(wxString group, wxString name);
};

#endif
