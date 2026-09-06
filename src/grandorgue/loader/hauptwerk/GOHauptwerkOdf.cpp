/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOHauptwerkOdf.h"

#include <wx/intl.h>
#include <wx/log.h>

#include "files/GOOpenedFile.h"

#include "GOBuffer.h"
#include "GOHash.h"

static const wxString WX_EMPTY = wxEmptyString;
static const wxString WX_ROOT_TAG = wxT("Hauptwerk");
static const wxString WX_OBJECT_LIST_TAG = wxT("ObjectList");
static const wxString WX_OBJECT_TYPE_ATTR = wxT("ObjectType");

const wxString &GOHauptwerkObject::Get(const wxString &name) const {
  const auto it = m_Attributes.find(name);

  return it == m_Attributes.end() ? WX_EMPTY : it->second;
}

bool GOHauptwerkObject::Has(const wxString &name) const {
  const auto it = m_Attributes.find(name);

  return it != m_Attributes.end() && !it->second.IsEmpty();
}

long GOHauptwerkObject::GetLong(const wxString &name, long defaultValue) const {
  const wxString &value = Get(name);
  long result = defaultValue;

  if (!value.IsEmpty() && !value.ToLong(&result))
    result = defaultValue;
  return result;
}

bool GOHauptwerkObject::IsYes(const wxString &name, bool isDefault) const {
  const wxString &value = Get(name);
  bool result = isDefault;

  if (!value.IsEmpty())
    result = value[0] == 'Y' || value[0] == 'y';
  return result;
}

/**
 * Resolves the XML entities Hauptwerk writes into text. Stop and organ names
 * routinely carry accented characters as numeric references - "P&#xE9;dale" -
 * and would otherwise reach the user with the escape still showing.
 */
static wxString decodeEntities(const wxString &text) {
  wxString result;

  if (text.Find(wxT('&')) == wxNOT_FOUND)
    result = text;
  else {
    const size_t textLen = text.Length();
    size_t pos = 0;

    result.reserve(textLen);
    while (pos < textLen) {
      const size_t ampI = text.find(wxT('&'), pos);
      const size_t endI
        = ampI == wxString::npos ? wxString::npos : text.find(wxT(';'), ampI);

      // A bare '&', or one too far from its ';' to be an entity, is literal.
      if (endI == wxString::npos || endI - ampI > 10) {
        result.append(text, pos, textLen - pos);
        pos = textLen;
      } else {
        result.append(text, pos, ampI - pos);

        const wxString entity = text.Mid(ampI + 1, endI - ampI - 1);
        bool isResolved = true;

        if (entity == wxT("amp"))
          result += wxT('&');
        else if (entity == wxT("lt"))
          result += wxT('<');
        else if (entity == wxT("gt"))
          result += wxT('>');
        else if (entity == wxT("quot"))
          result += wxT('"');
        else if (entity == wxT("apos"))
          result += wxT('\'');
        else if (entity.StartsWith(wxT("#"))) {
          const bool isHex
            = entity.Length() > 1 && (entity[1] == 'x' || entity[1] == 'X');
          unsigned long code = 0;

          if (entity.Mid(isHex ? 2 : 1).ToULong(&code, isHex ? 16 : 10) && code)
            result += wxUniChar(code);
          else
            isResolved = false;
        } else
          isResolved = false;

        if (!isResolved)
          // Not something we recognise: keep it verbatim rather than drop it.
          result.append(text, ampI, endI - ampI + 1);
        pos = endI + 1;
      }
    }
  }
  return result;
}

GOHauptwerkOdf::GOHauptwerkOdf() : m_IsCompressedFormat(false) {}

bool GOHauptwerkOdf::IsWanted(const wxString &objectType) const {
  return m_Filter.empty() || m_Filter.find(objectType) != m_Filter.end();
}

bool GOHauptwerkOdf::IsWanted(
  const wxString &objectType, const wxString &attribute) const {
  bool isWanted = true;

  if (!m_Filter.empty()) {
    const auto it = m_Filter.find(objectType);

    // An empty attribute set for a wanted type means "keep everything".
    isWanted = it != m_Filter.end()
      && (it->second.empty() || it->second.count(attribute) > 0);
  }
  return isWanted;
}

/**
 * Scans the flat <ObjectList>/<Object>/<Attribute> structure in one pass.
 *
 * This is deliberately not a general XML parser. The format has no
 * namespaces, no mixed content, no attributes other than ObjectType on
 * ObjectList, and never nests an object inside another - so a scanner that
 * tracks only the current object type and the current attribute name reads it
 * correctly while touching each character once. A DOM over 86 MB of this
 * would cost several hundred megabytes before any organ data existed.
 */
void GOHauptwerkOdf::ParseBuffer(const wxString &buffer) {
  const size_t bufferLen = buffer.Length();
  wxString objectType;   // the ObjectType of the enclosing ObjectList
  wxString attribute;    // element name one level below the object
  wxString text;         // character data collected for `attribute`
  GOHauptwerkObject object;
  bool isInObject = false;
  bool hasObject = false;
  size_t pos = 0;

  while (pos < bufferLen) {
    const size_t tagStart = buffer.find(wxT('<'), pos);

    if (tagStart == wxString::npos)
      break;

    if (isInObject && !attribute.IsEmpty() && tagStart > pos)
      text.append(buffer, pos, tagStart - pos);

    const size_t tagEnd = buffer.find(wxT('>'), tagStart);

    if (tagEnd == wxString::npos)
      break;

    const wxString tag = buffer.Mid(tagStart + 1, tagEnd - tagStart - 1);

    pos = tagEnd + 1;

    // Skip declarations and comments; everything else is structure.
    if (!tag.IsEmpty() && tag[0] != wxT('?') && tag[0] != wxT('!')) {
    const bool isClosing = tag[0] == wxT('/');
    const bool isSelfClosing = tag.Last() == wxT('/');
    // "Name" out of "<Name>", "</Name>" or "<Name/>"
    wxString name = tag;

    if (isClosing)
      name = name.Mid(1);
    else if (isSelfClosing)
      name = name.Left(name.Length() - 1);

    const size_t spaceI = name.find_first_of(wxT(" \t\r\n"));
    const wxString element
      = spaceI == wxString::npos ? name : name.Left(spaceI);

    if (element == WX_ROOT_TAG) {
      if (!isClosing) {
        const int verI = tag.Find(wxT("FileFormatVersion=\""));

        if (verI != wxNOT_FOUND) {
          const size_t valueI = verI + 19;

          m_FileFormatVersion
            = tag.Mid(valueI, tag.find(wxT('"'), valueI) - valueI);
        }
      }
    } else if (element == WX_OBJECT_LIST_TAG) {
      objectType.Clear();
      if (!isClosing) {
        const int typeI = tag.Find(WX_OBJECT_TYPE_ATTR + wxT("=\""));

        if (typeI != wxNOT_FOUND) {
          const size_t valueI = typeI + WX_OBJECT_TYPE_ATTR.Length() + 2;

          objectType = tag.Mid(valueI, tag.find(wxT('"'), valueI) - valueI);
        }
      }
    } else if (!objectType.IsEmpty()) {
      if (!isInObject) {
        // An element directly inside ObjectList opens an object. In the
        // compressed spelling it is <o> rather than the type name.
        if (!isClosing) {
          if (element == wxT("o") || element == wxT("O"))
            m_IsCompressedFormat = true;
          isInObject = true;
          hasObject = IsWanted(objectType);
          object = GOHauptwerkObject();
          attribute.Clear();
          if (isSelfClosing)
            isInObject = false;
        }
      } else if (attribute.IsEmpty()) {
        if (isClosing) {
          // closes the object itself
          if (hasObject)
            m_ObjectsByType[objectType].push_back(object);
          isInObject = false;
        } else if (!isSelfClosing) {
          attribute = element;
          text.Clear();
        }
        // a self-closing attribute is an empty value: nothing to record
      } else if (isClosing && element == attribute) {
        if (hasObject && IsWanted(objectType, attribute))
          object.Set(attribute, decodeEntities(text));
        attribute.Clear();
        text.Clear();
      }
    }
    }
  }
}

wxString GOHauptwerkOdf::Read(GOOpenedFile *pFile) {
  wxString errMsg;

  GOBuffer<char> content;

  if (!pFile->ReadContent(content))
    errMsg = _("Failed to read the Hauptwerk organ definition");
  else {
    GOHash hash;

    hash.Update(content.get(), content.GetCount());
    m_Hash = hash.getStringHash();

    // Hauptwerk writes these as UTF-8; fall back rather than lose the whole
    // file over one bad byte in a stop name.
    wxString buffer(content.get(), wxConvUTF8, content.GetCount());

    if (buffer.IsEmpty() && content.GetCount() > 0)
      buffer = wxString(content.get(), wxConvISO8859_1, content.GetCount());
    content.free();

    ParseBuffer(buffer);

    if (m_IsCompressedFormat)
      errMsg = _("This Hauptwerk organ definition uses the compressed format, "
                 "which is not supported yet");
    else if (m_ObjectsByType.empty())
      errMsg = _("No Hauptwerk objects found - is this an organ definition?");
  }
  return errMsg;
}

void GOHauptwerkOdf::EnsureIndexed(
  const wxString &objectType, const wxString &idAttribute) const {
  const wxString indexKey = objectType + wxT("\n") + idAttribute;

  if (m_IndexById.find(indexKey) == m_IndexById.end()) {
    auto &index = m_IndexById[indexKey];
    const auto it = m_ObjectsByType.find(objectType);

    if (it != m_ObjectsByType.end()) {
      const std::vector<GOHauptwerkObject> &objects = it->second;

      for (unsigned n = objects.size(), objectI = 0; objectI < n; objectI++) {
        const GOHauptwerkObject &object = objects[objectI];

        if (object.Has(idAttribute))
          index[object.GetLong(idAttribute)] = objectI;
      }
    }
  }
}

const std::vector<GOHauptwerkObject> &GOHauptwerkOdf::GetObjects(
  const wxString &objectType) const {
  static const std::vector<GOHauptwerkObject> EMPTY;
  const auto it = m_ObjectsByType.find(objectType);

  return it == m_ObjectsByType.end() ? EMPTY : it->second;
}

unsigned GOHauptwerkOdf::GetObjectCount(const wxString &objectType) const {
  return GetObjects(objectType).size();
}

const GOHauptwerkObject *GOHauptwerkOdf::FindById(
  const wxString &objectType, const wxString &idAttribute, long id) const {
  const GOHauptwerkObject *pResult = nullptr;

  EnsureIndexed(objectType, idAttribute);

  const auto indexIt = m_IndexById.find(objectType + wxT("\n") + idAttribute);

  if (indexIt != m_IndexById.end()) {
    const auto entryIt = indexIt->second.find(id);

    if (entryIt != indexIt->second.end())
      pResult = &m_ObjectsByType.at(objectType)[entryIt->second];
  }
  return pResult;
}
