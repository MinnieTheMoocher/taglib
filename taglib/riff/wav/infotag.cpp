/***************************************************************************
    copyright            : (C) 2012 by Tsuda Kageyu
    email                : tsuda.kageyu@gmail.com
 ***************************************************************************/

/***************************************************************************
 *   This library is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Lesser General Public License version   *
 *   2.1 as published by the Free Software Foundation.                     *
 *                                                                         *
 *   This library is distributed in the hope that it will be useful, but   *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with this library; if not, write to the Free Software   *
 *   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA         *
 *   02110-1301  USA                                                       *
 *                                                                         *
 *   Alternatively, this file is available under the Mozilla Public        *
 *   License Version 1.1.  You may obtain a copy of the License at         *
 *   http://www.mozilla.org/MPL/                                           *
 ***************************************************************************/

#include "infotag.h"

#include <utility>

#include "tbytevector.h"
#include "tdebug.h"
#include "tpropertymap.h"
#include "riffutils.h"

using namespace TagLib;
using namespace RIFF::Info;

namespace
{
  const RIFF::Info::StringHandler defaultStringHandler;
  const RIFF::Info::StringHandler *stringHandler = &defaultStringHandler;

  enum class InfoEncoding
  {
    Latin1,
    Windows1252,
    UTF8,
    Unsupported
  };

  InfoEncoding encodingForCodePage(unsigned int codePage)
  {
    switch(codePage) {
    case 0:
    case 28591:
      return InfoEncoding::Latin1;
    case 1252:
      return InfoEncoding::Windows1252;
    case 65001:
      return InfoEncoding::UTF8;
    default:
      return InfoEncoding::Unsupported;
    }
  }

  constexpr std::wstring::value_type windows1252HighBytes[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
  };

  String decodeWindows1252(const ByteVector &data)
  {
    std::wstring text;
    text.reserve(data.size());
    for(const char b : data) {
      if(b == '\0')
        break;
      const unsigned char c = static_cast<unsigned char>(b);
      if(c < 0x80 || c >= 0xA0)
        text.push_back(c);
      else
        text.push_back(windows1252HighBytes[c - 0x80]);
    }
    return String(text);
  }

  char encodeWindows1252Char(wchar_t c)
  {
    if(c < 0x80 || (c >= 0xA0 && c <= 0xFF))
      return static_cast<char>(c);

    for(unsigned int i = 0; i < 32; ++i) {
      if(windows1252HighBytes[i] == c)
        return static_cast<char>(0x80 + i);
    }

    return '?';
  }

  ByteVector encodeWindows1252(const String &s)
  {
    ByteVector out(static_cast<unsigned int>(s.size()), 0);
    char *p = out.data();
    for(const wchar_t c : s)
      *p++ = encodeWindows1252Char(c);
    return out;
  }

  class CodePageStringHandler : public RIFF::Info::StringHandler
  {
  public:
    explicit CodePageStringHandler(InfoEncoding encoding) :
      m_encoding(encoding)
    {
    }

    String parse(const ByteVector &data) const override
    {
      switch(m_encoding) {
      case InfoEncoding::UTF8:
        return String(data, String::UTF8);
      case InfoEncoding::Windows1252:
        return decodeWindows1252(data);
      case InfoEncoding::Latin1:
        return String(data, String::Latin1);
      case InfoEncoding::Unsupported:
        debug("RIFF::Info::Tag::parse() - Unsupported CSET code page, INFO field is skipped.");
        return String();
      }
      return String();
    }

    ByteVector render(const String &s) const override
    {
      switch(m_encoding) {
      case InfoEncoding::UTF8:
        return s.data(String::UTF8);
      case InfoEncoding::Windows1252:
        return encodeWindows1252(s);
      case InfoEncoding::Latin1:
        return s.data(String::Latin1);
      case InfoEncoding::Unsupported:
        return ByteVector();
      }
      return ByteVector();
    }

  private:
    InfoEncoding m_encoding;
  };

  const RIFF::Info::StringHandler *stringHandlerForCodePage(unsigned int codePage)
  {
    static const CodePageStringHandler latin1(InfoEncoding::Latin1);
    static const CodePageStringHandler windows1252(InfoEncoding::Windows1252);
    static const CodePageStringHandler utf8(InfoEncoding::UTF8);
    static const CodePageStringHandler unsupported(InfoEncoding::Unsupported);

    switch(encodingForCodePage(codePage)) {
    case InfoEncoding::Windows1252:
      return &windows1252;
    case InfoEncoding::UTF8:
      return &utf8;
    case InfoEncoding::Latin1:
      return &latin1;
    case InfoEncoding::Unsupported:
      return &unsupported;
    }
    return &unsupported;
  }
} // namespace

class RIFF::Info::Tag::TagPrivate
{
public:
  FieldListMap fieldListMap;

  const StringHandler *stringHandler = nullptr;
};

class RIFF::Info::StringHandler::StringHandlerPrivate
{
};

////////////////////////////////////////////////////////////////////////////////
// StringHandler implementation
////////////////////////////////////////////////////////////////////////////////

StringHandler::StringHandler() = default;

StringHandler::~StringHandler() = default;

String RIFF::Info::StringHandler::parse(const ByteVector &data) const
{
  return String(data, String::Latin1);
}

ByteVector RIFF::Info::StringHandler::render(const String &s) const
{
  return s.data(String::UTF8);
}

////////////////////////////////////////////////////////////////////////////////
// public members
////////////////////////////////////////////////////////////////////////////////

RIFF::Info::Tag::Tag(const ByteVector &data) :
  d(std::make_unique<TagPrivate>())
{
  parse(data);
}

RIFF::Info::Tag::Tag(const ByteVector &data, unsigned int codePage) :
  d(std::make_unique<TagPrivate>())
{
  d->stringHandler = stringHandlerForCodePage(codePage);
  parse(data);
}

RIFF::Info::Tag::Tag() :
  d(std::make_unique<TagPrivate>())
{
}

RIFF::Info::Tag::~Tag() = default;

String RIFF::Info::Tag::title() const
{
  return fieldText("INAM");
}

String RIFF::Info::Tag::artist() const
{
  return fieldText("IART");
}

String RIFF::Info::Tag::album() const
{
  return fieldText("IPRD");
}

String RIFF::Info::Tag::comment() const
{
  return fieldText("ICMT");
}

String RIFF::Info::Tag::genre() const
{
  return fieldText("IGNR");
}

unsigned int RIFF::Info::Tag::year() const
{
  return fieldText("ICRD").substr(0, 4).toInt();
}

unsigned int RIFF::Info::Tag::track() const
{
  return fieldText("IPRT").toInt();
}

void RIFF::Info::Tag::setTitle(const String &s)
{
  setFieldText("INAM", s);
}

void RIFF::Info::Tag::setArtist(const String &s)
{
  setFieldText("IART", s);
}

void RIFF::Info::Tag::setAlbum(const String &s)
{
  setFieldText("IPRD", s);
}

void RIFF::Info::Tag::setComment(const String &s)
{
  setFieldText("ICMT", s);
}

void RIFF::Info::Tag::setGenre(const String &s)
{
  setFieldText("IGNR", s);
}

void RIFF::Info::Tag::setYear(unsigned int i)
{
  if(i != 0)
    setFieldText("ICRD", String::number(i));
  else
    d->fieldListMap.erase("ICRD");
}

void RIFF::Info::Tag::setTrack(unsigned int i)
{
  if(i != 0)
    setFieldText("IPRT", String::number(i));
  else
    d->fieldListMap.erase("IPRT");
}

bool RIFF::Info::Tag::isEmpty() const
{
  return d->fieldListMap.isEmpty();
}

namespace
{
  const Map<ByteVector, String> propertyKeyForId = {
    {"IPRD", "ALBUM"},
    {"IENG", "ARRANGER"},
    {"IART", "ARTIST"},
    {"IBSU", "ARTISTWEBPAGE"},
    {"IBPM", "BPM"},
    {"ICMT", "COMMENT"},
    {"IMUS", "COMPOSER"},
    {"ICOP", "COPYRIGHT"},
    {"ICRD", "DATE"},
    {"PRT1", "DISCSUBTITLE"},
    {"ITCH", "ENCODEDBY"},
    {"ISFT", "ENCODING"},
    {"IDIT", "ENCODINGTIME"},
    {"IGNR", "GENRE"},
    {"ISRC", "ISRC"},
    {"IPUB", "LABEL"},
    {"ILNG", "LANGUAGE"},
    {"IWRI", "LYRICIST"},
    {"IMED", "MEDIA"},
    {"ISTR", "PERFORMER"},
    {"ICNT", "RELEASECOUNTRY"},
    {"IEDT", "REMIXER"},
    {"INAM", "TITLE"},
    {"IPRT", "TRACKNUMBER"}
  };
}  // namespace

PropertyMap RIFF::Info::Tag::properties() const
{
  PropertyMap props;
  for(const auto &[id, val] : std::as_const(d->fieldListMap)) {
    if(String key = propertyKeyForId.value(id); !key.isEmpty()) {
      props[key].append(val);
    }
    else {
      props.addUnsupportedData(key);
    }
  }
  return props;
}

void RIFF::Info::Tag::removeUnsupportedProperties(const StringList &props)
{
  for(const auto &id : props)
    d->fieldListMap.erase(id.data(String::Latin1));
}

PropertyMap RIFF::Info::Tag::setProperties(const PropertyMap &props)
{
  static const Map<String, ByteVector> idForPropertyKey = [] {
    Map<String, ByteVector> map;
    for(const auto &[id, key] : propertyKeyForId) {
      map[key] = id;
    }
    return map;
  }();

  const PropertyMap origProps = properties();
  for(const auto &[key, _] : origProps) {
    if(!props.contains(key) || props.value(key).isEmpty()) {
      d->fieldListMap.erase(idForPropertyKey.value(key));
    }
  }

  PropertyMap ignoredProps;
  for(const auto &[key, val] : props) {
    if(ByteVector id = idForPropertyKey.value(key);
       !id.isEmpty() && !val.isEmpty()) {
      d->fieldListMap[id] = val.front();
    }
    else {
      ignoredProps.insert(key, val);
    }
  }
  return ignoredProps;
}

FieldListMap RIFF::Info::Tag::fieldListMap() const
{
  return d->fieldListMap;
}

String RIFF::Info::Tag::fieldText(const ByteVector &id) const
{
  if(d->fieldListMap.contains(id))
    return String(d->fieldListMap[id]);
  return String();
}

void RIFF::Info::Tag::setFieldText(const ByteVector &id, const String &s)
{
  // id must be a four-byte long pure ascii string.
  if(!isValidChunkName(id))
    return;

  if(!s.isEmpty())
    d->fieldListMap[id] = s;
  else
    removeField(id);
}

void RIFF::Info::Tag::removeField(const ByteVector &id)
{
  if(d->fieldListMap.contains(id))
    d->fieldListMap.erase(id);
}

ByteVector RIFF::Info::Tag::render() const
{
  const StringHandler *handler = d->stringHandler ? d->stringHandler : stringHandler;
  ByteVector data("INFO");

  for(const auto &[field, list] : std::as_const(d->fieldListMap)) {
    ByteVector text = handler->render(list);
    if(text.isEmpty())
      continue;

    data.append(field);
    data.append(ByteVector::fromUInt(text.size() + 1, false));
    data.append(text);

    do {
      data.append('\0');
    } while(data.size() & 1);
  }

  if(data.size() == 4)
    return ByteVector();
  return data;
}

void RIFF::Info::Tag::setStringHandler(const StringHandler *handler)
{
  if(handler)
    stringHandler = handler;
  else
    stringHandler = &defaultStringHandler;
}

////////////////////////////////////////////////////////////////////////////////
// protected members
////////////////////////////////////////////////////////////////////////////////

void RIFF::Info::Tag::parse(const ByteVector &data)
{
  const StringHandler *handler = d->stringHandler ? d->stringHandler : stringHandler;
  unsigned int p = 4;
  while(p < data.size()) {
    const unsigned int size = data.toUInt(p + 4, false);
    if(size > data.size() - p - 8)
      break;

    if(const ByteVector id = data.mid(p, 4); isValidChunkName(id)) {
      const String text = handler->parse(data.mid(p + 8, size));
      d->fieldListMap[id] = text;
    }

    p += ((size + 1) & ~1) + 8;
  }
}
