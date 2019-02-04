////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016, goatpig                                               //            
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                   
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _H_DBUTILS
#define _H_DBUTILS

#include "BinaryData.h"

enum BLKDATA_TYPE
{
   NOT_BLKDATA,
   BLKDATA_HEADER,
   BLKDATA_TX,
   BLKDATA_TXOUT
};

enum DB_PREFIX
{
   DB_PREFIX_DBINFO,
   DB_PREFIX_HEADHASH,
   DB_PREFIX_HEADHGT,
   DB_PREFIX_TXDATA,
   DB_PREFIX_TXHINTS,
   DB_PREFIX_SCRIPT,
   DB_PREFIX_UNDODATA,
   DB_PREFIX_TRIENODES,
   DB_PREFIX_COUNT,
   DB_PREFIX_ZCDATA,
   DB_PREFIX_POOL,
   DB_PREFIX_MISSING_HASHES,
   DB_PREFIX_SUBSSH,
   DB_PREFIX_TEMPSCRIPT
};

struct FileMap
{
   size_t size_;
   uint8_t* filePtr_ = nullptr;

   void unmap(void);
};

class DBUtils
{
public:
   static const BinaryData ZeroConfHeader_;

public:

   static uint32_t   hgtxToHeight(const BinaryData& hgtx);
   static uint8_t    hgtxToDupID(const BinaryData& hgtx);
   static BinaryData heightAndDupToHgtx(uint32_t hgt, uint8_t dup);

   /////////////////////////////////////////////////////////////////////////////
   static BinaryData getBlkDataKey(uint32_t height,
      uint8_t  dup);

   /////////////////////////////////////////////////////////////////////////////
   static BinaryData getBlkDataKey(uint32_t height,
      uint8_t  dup,
      uint16_t txIdx);

   /////////////////////////////////////////////////////////////////////////////
   static BinaryData getBlkDataKey(uint32_t height,
      uint8_t  dup,
      uint16_t txIdx,
      uint16_t txOutIdx);

   /////////////////////////////////////////////////////////////////////////////
   static BinaryData getBlkDataKeyNoPrefix(uint32_t height,
      uint8_t  dup);

   /////////////////////////////////////////////////////////////////////////////
   static BinaryData getBlkDataKeyNoPrefix(uint32_t height,
      uint8_t  dup,
      uint16_t txIdx);

   /////////////////////////////////////////////////////////////////////////////
   static BinaryData getBlkDataKeyNoPrefix(uint32_t height,
      uint8_t  dup,
      uint16_t txIdx,
      uint16_t txOutIdx);



   /////////////////////////////////////////////////////////////////////////////
   static BLKDATA_TYPE readBlkDataKey(BinaryRefReader & brr,
      uint32_t & height,
      uint8_t  & dupID);

   /////////////////////////////////////////////////////////////////////////////
   static BLKDATA_TYPE readBlkDataKey(BinaryRefReader & brr,
      uint32_t & height,
      uint8_t  & dupID,
      uint16_t & txIdx);

   /////////////////////////////////////////////////////////////////////////////
   static BLKDATA_TYPE readBlkDataKey(BinaryRefReader & brr,
      uint32_t & height,
      uint8_t  & dupID,
      uint16_t & txIdx,
      uint16_t & txOutIdx);
   /////////////////////////////////////////////////////////////////////////////
   static BLKDATA_TYPE readBlkDataKeyNoPrefix(
      BinaryRefReader & brr,
      uint32_t & height,
      uint8_t  & dupID);

   /////////////////////////////////////////////////////////////////////////////
   static BLKDATA_TYPE readBlkDataKeyNoPrefix(
      BinaryRefReader & brr,
      uint32_t & height,
      uint8_t  & dupID,
      uint16_t & txIdx);

   /////////////////////////////////////////////////////////////////////////////
   static BLKDATA_TYPE readBlkDataKeyNoPrefix(
      BinaryRefReader & brr,
      uint32_t & height,
      uint8_t  & dupID,
      uint16_t & txIdx,
      uint16_t & txOutIdx);



   static std::string getPrefixName(uint8_t prefixInt);
   static std::string getPrefixName(DB_PREFIX pref);

   static bool checkPrefixByte(BinaryRefReader & brr,
      DB_PREFIX prefix,
      bool rewindWhenDone = false);
   static bool checkPrefixByteWError(BinaryRefReader & brr,
      DB_PREFIX prefix,
      bool rewindWhenDone = false);

   static BinaryData getFilterPoolKey(uint32_t filenum);
   static BinaryData getMissingHashesKey(uint32_t id);

   static bool fileExists(const std::string& path, int mode);

   static FileMap getMmapOfFile(const std::string&);
   
   static int removeDirectory(const std::string&);
   static struct stat getPathStat(const std::string& path);
   static struct stat getPathStat(const char* path, unsigned len);
   static size_t getFileSize(const std::string& path);
   static bool isFile(const std::string& path);
   static bool isDir(const std::string& path);

   static BinaryDataRef getDataRefForPacket(const BinaryDataRef& packet);
};
#endif