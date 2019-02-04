////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "EncryptionUtils.h"

using namespace std;

/////////////////////////////////////////////////////////////////////////////
KdfRomix::KdfRomix(void) :
   hashFunctionName_("sha512"),
   hashOutputBytes_(64),
   kdfOutputBytes_(32),
   memoryReqtBytes_(32),
   numIterations_(0)
{
   // Nothing to do here
}

/////////////////////////////////////////////////////////////////////////////
KdfRomix::KdfRomix(uint32_t memReqts, uint32_t numIter, SecureBinaryData salt) :
   hashFunctionName_("sha512"),
   hashOutputBytes_(64),
   kdfOutputBytes_(32)
{
   usePrecomputedKdfParams(memReqts, numIter, salt);
}

/////////////////////////////////////////////////////////////////////////////
void KdfRomix::computeKdfParams(double targetComputeSec, uint32_t maxMemReqts)
{
   // Create a random salt, even though this is probably unnecessary:
   // the variation in numIter and memReqts is probably effective enough
   salt_ = CryptoPRNG::generateRandom(32);

   // If target compute is 0s, then this method really only generates 
   // a random salt, and sets the other params to default minimum.
   if (targetComputeSec == 0)
   {
      numIterations_ = 1;
      memoryReqtBytes_ = 1024;
      return;
   }


   // Here, we pick the largest memory reqt that allows the executing system
   // to compute the KDF is less than the target time.  A maximum can be 
   // specified, in case the target system is likely to be memory-limited
   // more than compute-speed limited
   SecureBinaryData testKey("This is an example key to test KDF iteration speed");

   // Start the search for a memory value at 1kB
   memoryReqtBytes_ = 1024;
   double approxSec = 0;
   while (approxSec <= targetComputeSec / 4 && memoryReqtBytes_ < maxMemReqts)
   {
      memoryReqtBytes_ *= 2;

      sequenceCount_ = memoryReqtBytes_ / hashOutputBytes_;
      lookupTable_.resize(memoryReqtBytes_);

      TIMER_RESTART("KDF_Mem_Search");
      testKey = DeriveKey_OneIter(testKey);
      TIMER_STOP("KDF_Mem_Search");
      approxSec = TIMER_READ_SEC("KDF_Mem_Search");
   }

   // Recompute here, in case we didn't enter the search above 
   sequenceCount_ = memoryReqtBytes_ / hashOutputBytes_;
   lookupTable_.resize(memoryReqtBytes_);


   // Depending on the search above (or if a low max memory was chosen, 
   // we may need to do multiple iterations to achieve the desired compute
   // time on this system.
   double allItersSec = 0;
   uint32_t numTest = 1;
   while (allItersSec < 0.02)
   {
      numTest *= 2;
      TIMER_RESTART("KDF_Time_Search");
      for (uint32_t i = 0; i < numTest; i++)
      {
         SecureBinaryData _testKey("This is an example key to test KDF iteration speed");
         _testKey = DeriveKey_OneIter(_testKey);
      }
      TIMER_STOP("KDF_Time_Search");
      allItersSec = TIMER_READ_SEC("KDF_Time_Search");
   }

   double perIterSec = allItersSec / numTest;
   numIterations_ = (uint32_t)(targetComputeSec / (perIterSec + 0.0005));
   numIterations_ = (numIterations_ < 1 ? 1 : numIterations_);
   //cout << "System speed test results    :  " << endl;
   //cout << "   Total test of the KDF took:  " << allItersSec*1000 << " ms" << endl;
   //cout << "                   to execute:  " << numTest << " iterations" << endl;
   //cout << "   Target computation time is:  " << targetComputeSec*1000 << " ms" << endl;
   //cout << "   Setting numIterations to:    " << numIterations_ << endl;
}



/////////////////////////////////////////////////////////////////////////////
void KdfRomix::usePrecomputedKdfParams(uint32_t memReqts,
   uint32_t numIter,
   SecureBinaryData salt)
{
   memoryReqtBytes_ = memReqts;
   sequenceCount_ = memoryReqtBytes_ / hashOutputBytes_;
   numIterations_ = numIter;
   salt_ = salt;
}

/////////////////////////////////////////////////////////////////////////////
void KdfRomix::printKdfParams(void)
{
   // SHA512 computes 64-byte outputs
   cout << "KDF Parameters:" << endl;
   cout << "   HashFunction : " << hashFunctionName_ << endl;
   cout << "   HashOutBytes : " << hashOutputBytes_ << endl;
   cout << "   Memory/thread: " << memoryReqtBytes_ << " bytes" << endl;
   cout << "   SequenceCount: " << sequenceCount_ << endl;
   cout << "   NumIterations: " << numIterations_ << endl;
   cout << "   KDFOutBytes  : " << kdfOutputBytes_ << endl;
   cout << "   Salt         : " << salt_.toHexStr() << endl;
}


/////////////////////////////////////////////////////////////////////////////
SecureBinaryData KdfRomix::DeriveKey_OneIter(SecureBinaryData const & password)
{
   // Concatenate the salt/IV to the password
   SecureBinaryData saltedPassword = password + salt_;

   // Prepare the lookup table
   lookupTable_.resize(memoryReqtBytes_);
   lookupTable_.fill(0);
   uint32_t const HSZ = hashOutputBytes_;
   uint8_t* frontOfLUT = lookupTable_.getPtr();
   uint8_t* nextRead = NULL;
   uint8_t* nextWrite = NULL;

   // First hash to seed the lookup table, input is variable length anyway
   CryptoSHA2::getSha512(saltedPassword.getRef(), frontOfLUT);

   // Compute <sequenceCount_> consecutive hashes of the passphrase
   // Every iteration is stored in the next 64-bytes in the Lookup table
   for (uint32_t nByte = 0; nByte < memoryReqtBytes_ - HSZ; nByte += HSZ)
   {
      // Compute hash of slot i, put result in slot i+1
      nextRead = frontOfLUT + nByte;
      nextWrite = nextRead + hashOutputBytes_;
      BinaryDataRef bdr_next(nextRead, hashOutputBytes_);
      CryptoSHA2::getSha512(bdr_next, nextWrite);
   }

   // LookupTable should be complete, now start lookup sequence.
   // Start with the last hash from the previous step
   SecureBinaryData X(frontOfLUT + memoryReqtBytes_ - HSZ, HSZ);
   SecureBinaryData Y(HSZ);

   // We "integerize" a hash value by taking the last 4 bytes of
   // as a uint32_t, and take modulo sequenceCount
   uint64_t* X64ptr = (uint64_t*)(X.getPtr());
   uint64_t* Y64ptr = (uint64_t*)(Y.getPtr());
   uint64_t* V64ptr = NULL;
   uint32_t newIndex;
   uint32_t const nXorOps = HSZ / sizeof(uint64_t);

   // Pure ROMix would use sequenceCount_ for the number of lookups.
   // We divide by 2 to reduce computation time RELATIVE to the memory usage
   // This still provides suffient LUT operations, but allows us to use more
   // memory in the same amount of time (and this is the justification for
   // the scrypt algorithm -- it is basically ROMix, modified for more 
   // flexibility in controlling compute-time vs memory-usage).
   uint32_t const nLookups = sequenceCount_ / 2;
   for (uint32_t nSeq = 0; nSeq < nLookups; nSeq++)
   {
      // Interpret last 4 bytes of last result (mod seqCt) as next LUT index
      newIndex = *(uint32_t*)(X.getPtr() + HSZ - 4) % sequenceCount_;

      // V represents the hash result at <newIndex>
      V64ptr = (uint64_t*)(frontOfLUT + HSZ * newIndex);

      // xor X with V, and store the result in X
      for (uint32_t i = 0; i < nXorOps; i++)
         *(Y64ptr + i) = *(X64ptr + i) ^ *(V64ptr + i);

      // Hash the xor'd data to get the next index for lookup
      BinaryDataRef bdrY(Y.getPtr(), HSZ);
      CryptoSHA2::getSha512(bdrY, X.getPtr());
   }
   // Truncate the final result to get the final key
   lookupTable_.destroy();
   return X.getSliceCopy(0, kdfOutputBytes_);
}

/////////////////////////////////////////////////////////////////////////////
SecureBinaryData KdfRomix::DeriveKey(SecureBinaryData const & password)
{
   SecureBinaryData masterKey(password);
   for (uint32_t i = 0; i < numIterations_; i++)
      masterKey = DeriveKey_OneIter(masterKey);

   return SecureBinaryData(masterKey);
}
