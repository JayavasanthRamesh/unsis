/*
 * ResourceVersionInfo.cpp: implementation of the CResourceVersionInfo class.
 * 
 * This file is a part of NSIS.
 * 
 * Copyright (C) 1999-2009 Nullsoft and Contributors
 * 
 * Licensed under the zlib/libpng license (the "License");
 * you may not use this file except in compliance with the License.
 * 
 * Licence details can be found in the file COPYING.
 * 
 * This software is provided 'as-is', without any express or implied
 * warranty.
 * 
 * Modified for Unicode support by Jim Park -- 08/21/2007
 */

#include "ResourceVersionInfo.h"

#include "Platform.h"
#include "util.h"
#include "winchar.h"

#ifdef NSIS_SUPPORT_VERSION_INFO

#ifndef VOS__WINDOWS32
#  define VOS__WINDOWS32 4
#endif
#ifndef VFT_APP
#  define VFT_APP 1
#endif

#ifndef _WIN32
#  include <iconv.h>
#endif

// name should always be first on these structs using SortedStringListND.
struct version_string_list
{
  int name;
  int codepage;
  LANGID lang_id;
  DefineList *pChildStrings;
};

CVersionStrigList::~CVersionStrigList()
{
  struct version_string_list *itr = (struct version_string_list *) m_gr.get();
  int i = m_gr.getlen() / sizeof(struct version_string_list);

  while (i--)
  {
    delete itr[i].pChildStrings;
  }
}

int CVersionStrigList::add(LANGID langid, int codepage)
{
  TCHAR Buff[10];
  _stprintf(Buff, _T("%04x"), langid);
  int pos = SortedStringListND<struct version_string_list>::add(Buff);
  if (pos == -1) return false;
  
  version_string_list *data = ((version_string_list *)m_gr.get())+ pos;
  data->pChildStrings = new DefineList;
  data->codepage      = codepage;
  data->lang_id       = langid;
  return pos;
}

LANGID CVersionStrigList::get_lang(int idx)
{
  version_string_list *data=(version_string_list *)m_gr.get();
  return data[idx].lang_id;
}

int CVersionStrigList::get_codepage(int idx)
{
  version_string_list *data=(version_string_list *)m_gr.get();
  return data[idx].codepage;
}

DefineList* CVersionStrigList::get_strings(int idx)
{
  version_string_list *data=(version_string_list *)m_gr.get();
  return data[idx].pChildStrings;
}

int CVersionStrigList::find(LANGID lang_id, int codepage)
{
  TCHAR Buff[10];
  _stprintf(Buff, _T("%04x"), lang_id);
  return SortedStringListND<struct version_string_list>::find(Buff);
}

int CVersionStrigList::getlen()
{
  return (m_strings.getlen()/sizeof(TCHAR));
}

int CVersionStrigList::getnum()
{
  return m_gr.getlen()/sizeof(struct version_string_list);
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
CResourceVersionInfo::CResourceVersionInfo()
{
    memset(&m_FixedInfo, 0, sizeof(VS_FIXEDFILEINFO));
    m_FixedInfo.dwSignature = 0xFEEF04BD;
    m_FixedInfo.dwFileOS = VOS__WINDOWS32;
    m_FixedInfo.dwFileType = VFT_APP;
}

CResourceVersionInfo::~CResourceVersionInfo()
{
    
}

void CResourceVersionInfo::SetFileFlags(int Value)
{
    // Jim Park: This looks wrong.  This will only set the dwFileFlags to true
    // or false meaning 0 or 1.  But it's also never used, so I guess it's
    // fine. 
	 // m_FixedInfo.dwFileFlags = (m_FixedInfo.dwFileFlags & ~(m_FixedInfo.dwFileFlagsMask)) || Value;
    // 
    // Jim Park: My fix is below.  This also does no good since
    // dwFileFlagsMask does not get changed from 0 set in the constructor.  So
    // dwFileFlags will stay zero since no  flags are set to be legal in the
    // mask.
	 m_FixedInfo.dwFileFlags |= Value & m_FixedInfo.dwFileFlagsMask;
}

void CResourceVersionInfo::SetFileVersion(int HighPart, int LowPart)
{
    m_FixedInfo.dwFileVersionLS = LowPart;
    m_FixedInfo.dwFileVersionMS = HighPart;
}

void CResourceVersionInfo::SetProductVersion(int HighPart, int LowPart)
{
    m_FixedInfo.dwProductVersionLS = LowPart;
    m_FixedInfo.dwProductVersionMS = HighPart;
}

// Jim Park: Not sure where this is used.
int GetVersionHeader (LPSTR &p, WORD &wLength, WORD &wValueLength, WORD &wType)
{
    WCHAR *szKey;
    char * baseP;
    
    baseP = p;
    wLength = *(WORD*)p;
    p += sizeof(WORD);
    wValueLength = *(WORD*)p;
    p += sizeof(WORD);
    wType = *(WORD*)p;
    p += sizeof(WORD);
    szKey = (WCHAR*)p;
    p += (winchar_strlen(szKey) + 1) * sizeof (WCHAR);
    while ( ((long)p % 4) != 0 )
        p++;
    return p - baseP;    
}

DWORD ZEROS = 0;

void PadStream (GrowBuf &strm)
{
    if ( (strm.getlen() % 4) != 0 )
        strm.add (&ZEROS, 4 - (strm.getlen() % 4));
}

// Helper function only used by CResourceVersionInfo::ExportToStream
// Cannot handle anything longer than 65K objects.
//
// @param wLength Size in bytes of the entire object we are storing.
// @param wValueLength The value length in bytes.
// @param wType If type is 1, it's a wchar_t string, so save value length appropriately.
// @param key The string key
// @param value The value mapped to string key.
void SaveVersionHeader (GrowBuf &strm, WORD wLength, WORD wValueLength, WORD wType, const WCHAR *key, void *value)
{
    WORD valueLen;
    WORD keyLen;
    
    strm.add (&wLength, sizeof (wLength));
    
    strm.add (&wValueLength, sizeof (wValueLength));
    strm.add (&wType, sizeof (wType));
    keyLen = WORD((winchar_strlen(key) + 1) * sizeof (WCHAR));
    strm.add ((void*)key, keyLen);
    
    PadStream(strm);
    
    if ( wValueLength > 0 )
    {
        valueLen = wValueLength;
        if ( wType == 1 )
            valueLen = valueLen * WORD(sizeof (WCHAR));
        strm.add (value, valueLen);
    }
}

void CResourceVersionInfo::ExportToStream(GrowBuf &strm, int Index)
{
    DWORD v;
    WORD wSize;  
    int p, p1;
    WCHAR *KeyName, *KeyValue;

    strm.resize(0);
    SaveVersionHeader (strm, 0, sizeof (VS_FIXEDFILEINFO), 0, L"VS_VERSION_INFO", &m_FixedInfo);
    
    DefineList *pChildStrings = m_ChildStringLists.get_strings(Index);
    if ( pChildStrings->getnum() > 0 )
    {
      GrowBuf stringInfoStream;
      int codepage = m_ChildStringLists.get_codepage(Index);
      LANGID langid = m_ChildStringLists.get_lang(Index);
      WCHAR Buff[16];
      swprintf(Buff, sizeof(Buff)/sizeof(Buff[0]), L"%04x%04x", langid, codepage);
      SaveVersionHeader (stringInfoStream, 0, 0, 0, Buff, &ZEROS);
      
      for ( int i = 0; i < pChildStrings->getnum(); i++ )
      {
        PadStream (stringInfoStream);
        
        p = stringInfoStream.getlen();
#ifdef _UNICODE
        KeyName = pChildStrings->getname(i);
        KeyValue = pChildStrings->getvalue(i);
        SaveVersionHeader (stringInfoStream, 0, WORD(winchar_strlen(KeyValue) + 1), 1, KeyName, (void*)KeyValue);
#else
        KeyName = winchar_fromansi(pChildStrings->getname(i), codepage);
        KeyValue = winchar_fromansi(pChildStrings->getvalue(i), codepage);
        SaveVersionHeader (stringInfoStream, 0, WORD(winchar_strlen(KeyValue) + 1), 1, KeyName, (void*)KeyValue);
        delete [] KeyName;
        delete [] KeyValue;
#endif
        wSize = WORD(stringInfoStream.getlen() - p);
        
        *(WORD*)((PBYTE)stringInfoStream.get()+p)=wSize;
      }
      
      wSize = WORD(stringInfoStream.getlen());
      *(WORD*)((PBYTE)stringInfoStream.get())=wSize;
      
      PadStream (strm);
      p = strm.getlen();
      //KeyName = winchar_fromansi("StringFileInfo", CP_ACP);
      KeyName = L"StringFileInfo";
      SaveVersionHeader (strm, 0, 0, 0, KeyName, &ZEROS);
      //delete [] KeyName;
      strm.add (stringInfoStream.get(), stringInfoStream.getlen());
      wSize = WORD(strm.getlen() - p);
      
      *(WORD*)((PBYTE)strm.get()+p)=wSize;
    }

    // Show all languages avaiable using Var-Translations
    if ( m_ChildStringLists.getnum() > 0 )
    {
      PadStream (strm);
      p = strm.getlen();
      //KeyName = winchar_fromansi("VarFileInfo", CP_ACP);
      KeyName = L"VarFileInfo";
      SaveVersionHeader (strm, 0, 0, 0, KeyName, &ZEROS);
      //delete [] KeyName;
      PadStream (strm);
      
      p1 = strm.getlen();
      //KeyName = winchar_fromansi("Translation", CP_ACP);
      KeyName = L"Translation";
      SaveVersionHeader (strm, 0, 0, 0, KeyName, &ZEROS);
      //delete [] KeyName;
      
      // First add selected code language translation
      v = MAKELONG(m_ChildStringLists.get_lang(Index), m_ChildStringLists.get_codepage(Index));
      strm.add (&v, sizeof (v));

      for ( int k =0; k < m_ChildStringLists.getnum(); k++ )
      {
        if ( k != Index )
        {
          v = MAKELONG(m_ChildStringLists.get_lang(k), m_ChildStringLists.get_codepage(k));
          strm.add (&v, sizeof (v));
        }
      }
      
      wSize = WORD(strm.getlen() - p1);
      *(WORD*)((PBYTE)strm.get()+p1)=wSize;
      wSize = WORD(sizeof (int) * m_ChildStringLists.getnum());
      p1+=sizeof(WORD);
      *(WORD*)((PBYTE)strm.get()+p1)=wSize;
      
      wSize = WORD(strm.getlen() - p);
      *(WORD*)((PBYTE)strm.get()+p)=wSize;
    }
    
    wSize = WORD(strm.getlen());
    *(WORD*)((PBYTE)strm.get())=wSize;
}

// Returns 0 if success, 1 if already defined
int CResourceVersionInfo::SetKeyValue(LANGID lang_id, int codepage, TCHAR* AKeyName, TCHAR* AValue)
{
  int pos = m_ChildStringLists.find(lang_id, codepage);
  if ( pos == -1 )
  {
    pos = m_ChildStringLists.add(lang_id, codepage);
  }
  DefineList *pStrings = m_ChildStringLists.get_strings(pos);
  return pStrings->add(AKeyName, AValue);
}

int CResourceVersionInfo::GetStringTablesCount()
{
  return m_ChildStringLists.getnum();
}

LANGID CResourceVersionInfo::GetLangID(int Index)
{
  return m_ChildStringLists.get_lang(Index);
}

int CResourceVersionInfo::GetCodePage(int Index)
{
  return m_ChildStringLists.get_codepage(Index);
}

TCHAR *CResourceVersionInfo::FindKey(LANGID LangID, int codepage, const TCHAR *pKeyName)
{
  int pos = m_ChildStringLists.find(LangID, codepage);
  if ( pos == -1 )
  {
    return NULL;
  }
  DefineList *pStrings = m_ChildStringLists.get_strings(pos);
  return pStrings->find(pKeyName);
}

#endif
