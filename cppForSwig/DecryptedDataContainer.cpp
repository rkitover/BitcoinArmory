////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "DecryptedDataContainer.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// DecryptedDataContainer
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DecryptedDataContainer::initAfterLock()
{
   auto&& decryptedDataInstance = make_unique<DecryptedData>();

   //copy default encryption key
   auto&& defaultEncryptionKeyCopy = defaultEncryptionKey_.copy();

   auto defaultKey =
      make_unique<DecryptedEncryptionKey>(defaultEncryptionKeyCopy);
   decryptedDataInstance->encryptionKeys_.insert(make_pair(
      defaultEncryptionKeyId_, move(defaultKey)));

   lockedDecryptedData_ = move(decryptedDataInstance);
}

////////////////////////////////////////////////////////////////////////////////
void DecryptedDataContainer::cleanUpBeforeUnlock()
{
   otherLocks_.clear();
   lockedDecryptedData_.reset();
}

////////////////////////////////////////////////////////////////////////////////
void DecryptedDataContainer::lockOther(
   shared_ptr<DecryptedDataContainer> other)
{
   if (!ownsLock())
      throw DecryptedDataContainerException("unlocked/does not own lock");

   if (lockedDecryptedData_ == nullptr)
      throw DecryptedDataContainerException(
      "nullptr lock! how did we get this far?");

   otherLocks_.push_back(OtherLockedContainer(other));
}

////////////////////////////////////////////////////////////////////////////////
unique_ptr<DecryptedEncryptionKey>
DecryptedDataContainer::deriveEncryptionKey(
unique_ptr<DecryptedEncryptionKey> decrKey, const BinaryData& kdfid) const
{
   //sanity check
   if (!ownsLock())
      throw DecryptedDataContainerException("unlocked/does not own lock");

   if (lockedDecryptedData_ == nullptr)
      throw DecryptedDataContainerException(
      "nullptr lock! how did we get this far?");

   //does the decryption key have this derivation?
   auto derivationIter = decrKey->derivedKeys_.find(kdfid);
   if (derivationIter == decrKey->derivedKeys_.end())
   {
      //look for the kdf
      auto kdfIter = kdfMap_.find(kdfid);
      if (kdfIter == kdfMap_.end() || kdfIter->second == nullptr)
         throw DecryptedDataContainerException("can't find kdf params for id");

      //derive the key, this will insert it into the container too
      decrKey->deriveKey(kdfIter->second);
   }

   return move(decrKey);
}

////////////////////////////////////////////////////////////////////////////////
const SecureBinaryData& DecryptedDataContainer::getDecryptedPrivateKey(
   shared_ptr<Asset_PrivateKey> dataPtr)
{
   //sanity check
   if (!ownsLock())
      throw DecryptedDataContainerException("unlocked/does not own lock");

   if (lockedDecryptedData_ == nullptr)
      throw DecryptedDataContainerException(
      "nullptr lock! how did we get this far?");

   auto insertDecryptedData = [this](unique_ptr<DecryptedPrivateKey> decrKey)->
      const SecureBinaryData&
   {
      //if decrKey is empty, all casts failed, throw
      if (decrKey == nullptr)
      throw DecryptedDataContainerException("unexpected dataPtr type");

      //make sure insertion succeeds
      lockedDecryptedData_->privateKeys_.erase(decrKey->getId());
      auto&& keypair = make_pair(decrKey->getId(), move(decrKey));
      auto&& insertionPair =
         lockedDecryptedData_->privateKeys_.insert(move(keypair));

      return insertionPair.first->second->getDataRef();
   };

   //look for already decrypted data
   auto dataIter = lockedDecryptedData_->privateKeys_.find(dataPtr->getId());
   if (dataIter != lockedDecryptedData_->privateKeys_.end())
      return dataIter->second->getDataRef();

   //no decrypted val entry, let's try to decrypt the data instead

   if (!dataPtr->hasData())
   {
      //missing encrypted data in container (most likely uncomputed private key)
      //throw back to caller, this object only deals with decryption
      throw EncryptedDataMissing();
   }

   //check cypher
   if (dataPtr->cypher_ == nullptr)
   {
      //null cypher, data is not encrypted, create entry and return it
      auto dataCopy = dataPtr->data_;
      auto&& decrKey = make_unique<DecryptedPrivateKey>(
         dataPtr->getId(), dataCopy);
      return insertDecryptedData(move(decrKey));
   }

   //we have a valid cypher, grab the encryption key
   unique_ptr<DecryptedEncryptionKey> decrKey;
   auto& encryptionKeyId = dataPtr->cypher_->getEncryptionKeyId();
   auto& kdfId = dataPtr->cypher_->getKdfId();

   populateEncryptionKey(encryptionKeyId, kdfId);

   auto decrKeyIter =
      lockedDecryptedData_->encryptionKeys_.find(encryptionKeyId);
   if (decrKeyIter == lockedDecryptedData_->encryptionKeys_.end())
      throw DecryptedDataContainerException("could not get encryption key");

   auto derivationKeyIter = decrKeyIter->second->derivedKeys_.find(kdfId);
   if (derivationKeyIter == decrKeyIter->second->derivedKeys_.end())
      throw DecryptedDataContainerException("could not get derived encryption key");

   //decrypt data
   auto decryptedDataPtr = move(dataPtr->decrypt(derivationKeyIter->second));

   //sanity check
   if (decryptedDataPtr == nullptr)
      throw DecryptedDataContainerException("failed to decrypt data");

   //insert the newly decrypted data in the container and return
   return insertDecryptedData(move(decryptedDataPtr));
}

////////////////////////////////////////////////////////////////////////////////
void DecryptedDataContainer::populateEncryptionKey(
   const BinaryData& keyid, const BinaryData& kdfid)
{
   //sanity check
   if (!ownsLock())
      throw DecryptedDataContainerException("unlocked/does not own lock");

   if (lockedDecryptedData_ == nullptr)
      throw DecryptedDataContainerException(
      "nullptr lock! how did we get this far?");

   //lambda to insert keys back into the container
   auto insertDecryptedData = [&keyid, this](
      unique_ptr<DecryptedEncryptionKey> decrKey)->void
   {
      //if decrKey is empty, all casts failed, throw
      if (decrKey == nullptr)
         throw DecryptedDataContainerException(
         "tried to insert empty decryption key");

      //make sure insertion succeeds
      lockedDecryptedData_->encryptionKeys_.erase(keyid);
      auto&& keypair = make_pair(keyid, move(decrKey));
      auto&& insertionPair =
         lockedDecryptedData_->encryptionKeys_.insert(move(keypair));
   };

   //look for already decrypted data
   unique_ptr<DecryptedEncryptionKey> decryptedKey = nullptr;
   auto dataIter = lockedDecryptedData_->encryptionKeys_.find(keyid);
   if (dataIter != lockedDecryptedData_->encryptionKeys_.end())
      decryptedKey = move(dataIter->second);

   if (decryptedKey == nullptr)
   {
      //we don't have a decrypted key, let's look for it in the encrypted map
      auto encrKeyIter = encryptionKeyMap_.find(keyid);
      if (encrKeyIter != encryptionKeyMap_.end())
      {
         //sanity check
         auto encryptedKeyPtr = dynamic_pointer_cast<Asset_EncryptionKey>(
            encrKeyIter->second);
         if (encryptedKeyPtr == nullptr)
            throw DecryptedDataContainerException(
            "unexpected object for encryption key id");

         //found the encrypted key, need to decrypt it first
         populateEncryptionKey(
            encryptedKeyPtr->cypher_->getEncryptionKeyId(),
            encryptedKeyPtr->cypher_->getKdfId());

         //grab encryption key from map
         auto decrKeyIter =
            lockedDecryptedData_->encryptionKeys_.find(
            encryptedKeyPtr->cypher_->getEncryptionKeyId());
         if (decrKeyIter == lockedDecryptedData_->encryptionKeys_.end())
            throw DecryptedDataContainerException("failed to decrypt key");
         auto&& decryptionKey = move(decrKeyIter->second);

         //derive encryption key
         decryptionKey = move(
            deriveEncryptionKey(move(decryptionKey),
            encryptedKeyPtr->cypher_->getKdfId()));

         //decrypt encrypted key
         auto&& rawDecryptedKey = encryptedKeyPtr->cypher_->decrypt(
            decryptionKey->getDerivedKey(encryptedKeyPtr->cypher_->getKdfId()),
            encryptedKeyPtr->data_);

         decryptedKey = move(make_unique<DecryptedEncryptionKey>(
            rawDecryptedKey));

         //move decryption key back to container
         insertDecryptedData(move(decryptionKey));
      }
   }

   if (decryptedKey == nullptr)
   {
      //still no key, prompt the user
      decryptedKey = move(promptPassphrase(keyid, kdfid));
   }

   //apply kdf
   decryptedKey = move(deriveEncryptionKey(move(decryptedKey), kdfid));

   //insert into map
   insertDecryptedData(move(decryptedKey));
}

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData DecryptedDataContainer::encryptData(
   Cypher* const cypher, const SecureBinaryData& data)
{
   //sanity check
   if (cypher == nullptr)
      throw DecryptedDataContainerException("null cypher");

   if (!ownsLock())
      throw DecryptedDataContainerException("unlocked/does not own lock");

   if (lockedDecryptedData_ == nullptr)
      throw DecryptedDataContainerException(
      "nullptr lock! how did we get this far?");

   populateEncryptionKey(cypher->getEncryptionKeyId(), cypher->getKdfId());
   auto keyIter = lockedDecryptedData_->encryptionKeys_.find(
      cypher->getEncryptionKeyId());
   auto& derivedKey = keyIter->second->getDerivedKey(cypher->getKdfId());

   return move(cypher->encrypt(derivedKey, data));
}

////////////////////////////////////////////////////////////////////////////////
unique_ptr<DecryptedEncryptionKey> DecryptedDataContainer::promptPassphrase(
   const BinaryData& keyId, const BinaryData& kdfId) const
{
   while (1)
   {
      if (!getPassphraseLambda_)
         throw DecryptedDataContainerException("empty passphrase lambda");

      auto&& passphrase = getPassphraseLambda_(keyId);

      if (passphrase.getSize() == 0)
         throw DecryptedDataContainerException("empty passphrase");

      auto keyPtr = make_unique<DecryptedEncryptionKey>(passphrase);
      keyPtr = move(deriveEncryptionKey(move(keyPtr), kdfId));

      if (keyId == keyPtr->getId(kdfId))
         return move(keyPtr);
   }

   return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
void DecryptedDataContainer::updateKeyOnDisk(
   const BinaryData& key, shared_ptr<Asset_EncryptedData> dataPtr)
{
   //serialize db key
   auto&& dbKey = WRITE_UINT8_BE(ENCRYPTIONKEY_PREFIX);
   dbKey.append(key);

   updateKeyOnDiskNoPrefix(dbKey, dataPtr);
}

////////////////////////////////////////////////////////////////////////////////
void DecryptedDataContainer::updateKeyOnDiskNoPrefix(
   const BinaryData& dbKey, shared_ptr<Asset_EncryptedData> dataPtr)
{
   /*caller needs to manage db tx*/

   //check if data is on disk already
   CharacterArrayRef keyRef(dbKey.getSize(), dbKey.getPtr());
   auto&& dataRef = dbPtr_->get_NoCopy(keyRef);

   if (dataRef.len != 0)
   {
      BinaryDataRef bdr((uint8_t*)dataRef.data, dataRef.len);
      //already have this key, is it the same data?
      auto onDiskData = Asset_EncryptedData::deserialize(bdr);

      //data has not changed, no need to commit
      if (onDiskData->isSame(dataPtr.get()))
         return;

      //data has changed, wipe the existing data
      deleteKeyFromDisk(dbKey);
   }

   auto&& serializedData = dataPtr->serialize();
   CharacterArrayRef dataRef_Put(
      serializedData.getSize(), serializedData.getPtr());
   dbPtr_->insert(keyRef, dataRef_Put);

}

////////////////////////////////////////////////////////////////////////////////
void DecryptedDataContainer::updateOnDisk()
{
   //wallet needs to create the db read/write tx

   //encryption keys
   for (auto& key : encryptionKeyMap_)
      updateKeyOnDisk(key.first, key.second);

   //kdf
   for (auto& key : kdfMap_)
   {
      //get db key
      auto&& dbKey = WRITE_UINT8_BE(KDF_PREFIX);
      dbKey.append(key.first);

      //fetch from db
      CharacterArrayRef keyRef(dbKey.getSize(), dbKey.getPtr());
      auto&& dataRef = dbPtr_->get_NoCopy(keyRef);

      if (dataRef.len != 0)
      {
         BinaryDataRef bdr((uint8_t*)dataRef.data, dataRef.len);
         //already have this key, is it the same data?
         auto onDiskData = KeyDerivationFunction::deserialize(bdr);

         //data has not changed, not commiting to disk
         if (onDiskData->isSame(key.second.get()))
            continue;

         //data has changed, wipe the existing data
         deleteKeyFromDisk(dbKey);
      }

      auto&& serializedData = key.second->serialize();
      CharacterArrayRef dataRef_Put(
         serializedData.getSize(), serializedData.getPtr());
      dbPtr_->insert(keyRef, dataRef_Put);
   }
}

////////////////////////////////////////////////////////////////////////////////
void DecryptedDataContainer::deleteKeyFromDisk(const BinaryData& key)
{
   /***
   This operation abuses the no copy read feature in lmdb. Since all data is
   mmap'd, a no copy read is a pointer to the data on disk. Therefor modifying
   that data will result in a modification on disk.

   This is done under 3 conditions:
   1) The decrypted data container is locked.
   2) The calling threads owns a ReadWrite transaction on the lmdb object
   3) There are no active ReadOnly transactions on the lmdb object

   1. is a no brainer, 2. guarantees the changes are flushed to disk once the
   tx is released. RW tx are locked, therefor only one is active at any given
   time, by LMDB design.

   3. is to guarantee there are no readers when the change takes place. Needs
   some LMDB C++ wrapper modifications to be able to check from the db object.
   The condition should be enforced by the caller regardless.
   ***/

   //sanity checks
   if (!ownsLock())
      throw DecryptedDataContainerException("unlocked/does not own lock");

   //check db only has one RW tx
   /*if (!dbEnv_->isRWLockExclusive())
   {
   throw DecryptedDataContainerException(
   "need exclusive RW lock to delete entries");
   }

   //check we own the RW tx
   if (dbEnv_->ownsLock() != LMDB_RWLOCK)
   {
   throw DecryptedDataContainerException(
   "need exclusive RW lock to delete entries");
   }*/

   CharacterArrayRef keyRef(key.getSize(), key.getCharPtr());

   //check data exist son disk to begin with
   {
      auto dataRef = dbPtr_->get_NoCopy(keyRef);

      //data is empty, nothing to wipe
      if (dataRef.len == 0)
      {
         throw DecryptedDataContainerException(
            "tried to wipe non existent entry");
      }
   }

   //wipe it
   dbPtr_->wipe(keyRef);
}

////////////////////////////////////////////////////////////////////////////////
void DecryptedDataContainer::readFromDisk()
{
   {
      //encryption key and kdf entries
      auto dbIter = dbPtr_->begin();

      BinaryWriter bwEncrKey;
      bwEncrKey.put_uint8_t(ENCRYPTIONKEY_PREFIX);

      CharacterArrayRef keyRef(bwEncrKey.getSize(), bwEncrKey.getData().getPtr());

      dbIter.seek(keyRef, LMDB::Iterator::Seek_GE);

      while (dbIter.isValid())
      {
         auto iterkey = dbIter.key();
         auto itervalue = dbIter.value();

         if (iterkey.mv_size < 2)
            throw runtime_error("empty db key");

         if (itervalue.mv_size < 1)
            throw runtime_error("empty value");

         BinaryDataRef keyBDR((uint8_t*)iterkey.mv_data + 1, iterkey.mv_size - 1);
         BinaryDataRef valueBDR((uint8_t*)itervalue.mv_data, itervalue.mv_size);

         auto prefix = (uint8_t*)iterkey.mv_data;
         switch (*prefix)
         {
         case ENCRYPTIONKEY_PREFIX:
         {
            auto keyPtr = Asset_EncryptedData::deserialize(valueBDR);
            auto encrKeyPtr = dynamic_pointer_cast<Asset_EncryptionKey>(keyPtr);
            if (encrKeyPtr == nullptr)
               throw runtime_error("empty keyptr");

            addEncryptionKey(encrKeyPtr);

            break;
         }

         case KDF_PREFIX:
         {
            auto kdfPtr = KeyDerivationFunction::deserialize(valueBDR);
            if (keyBDR != kdfPtr->getId())
               throw runtime_error("kdf id mismatch");

            addKdf(kdfPtr);
            break;
         }
         }

         dbIter.advance();
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
void DecryptedDataContainer::encryptEncryptionKey(
   const BinaryData& keyID,
   const SecureBinaryData& newPassphrase)
{
   /***
   Encrypts an encryption key with "newPassphrase". If the key is already
   encrypted, it will be changed.
   ***/

   //sanity check
   if (!ownsLock())
      throw DecryptedDataContainerException("unlocked/does not own lock");

   if (lockedDecryptedData_ == nullptr)
      throw DecryptedDataContainerException(
      "nullptr lock! how did we get this far?");

   auto keyIter = encryptionKeyMap_.find(keyID);
   if (keyIter == encryptionKeyMap_.end())
      throw DecryptedDataContainerException(
      "cannot change passphrase for unknown key");

   //decrypt master encryption key
   auto& kdfId = keyIter->second->cypher_->getKdfId();
   populateEncryptionKey(keyID, kdfId);

   //grab decrypted key
   auto decryptedKeyIter = lockedDecryptedData_->encryptionKeys_.find(keyID);
   if (decryptedKeyIter == lockedDecryptedData_->encryptionKeys_.end())
      throw DecryptedDataContainerException(
      "failed to decrypt key");

   auto& decryptedKey = decryptedKeyIter->second->getData();

   //grab kdf for key id computation
   auto masterKeyKdfId = keyIter->second->cypher_->getKdfId();
   auto kdfIter = kdfMap_.find(masterKeyKdfId);
   if (kdfIter == kdfMap_.end())
      throw DecryptedDataContainerException("failed to grab kdf");

   //copy passphrase cause the ctor will move the data in
   auto newPassphraseCopy = newPassphrase;

   //kdf the key to get its id
   auto newEncryptionKey = make_unique<DecryptedEncryptionKey>(newPassphraseCopy);
   newEncryptionKey->deriveKey(kdfIter->second);
   auto newKeyId = newEncryptionKey->getId(masterKeyKdfId);

   //create new cypher, pointing to the new key id
   auto newCypher = keyIter->second->cypher_->getCopy(newKeyId);

   //add new encryption key object to container
   lockedDecryptedData_->encryptionKeys_.insert(
      move(make_pair(newKeyId, move(newEncryptionKey))));

   //encrypt master key
   auto&& newEncryptedKey = encryptData(newCypher.get(), decryptedKey);

   //create new encrypted container
   auto keyIdCopy = keyID;
   auto newEncryptedKeyPtr =
      make_shared<Asset_EncryptionKey>(keyIdCopy, newEncryptedKey, move(newCypher));

   //update
   keyIter->second = newEncryptedKeyPtr;

   auto&& temp_key = WRITE_UINT8_BE(ENCRYPTIONKEY_PREFIX_TEMP);
   temp_key.append(keyID);
   auto&& perm_key = WRITE_UINT8_BE(ENCRYPTIONKEY_PREFIX);
   perm_key.append(keyID);

   {
      //write new encrypted key as temp key within it's own transaction
      LMDBEnv::Transaction tempTx(dbEnv_, LMDB::ReadWrite);
      updateKeyOnDiskNoPrefix(temp_key, newEncryptedKeyPtr);
   }

   {
      LMDBEnv::Transaction permTx(dbEnv_, LMDB::ReadWrite);

      //wipe old key from disk
      deleteKeyFromDisk(perm_key);

      //write new key to disk
      updateKeyOnDiskNoPrefix(perm_key, newEncryptedKeyPtr);
   }

   {
      LMDBEnv::Transaction permTx(dbEnv_, LMDB::ReadWrite);

      //wipe temp entry
      deleteKeyFromDisk(temp_key);
   }
}
