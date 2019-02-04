////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2018, goatpig                                          //            
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                   
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _BDV_NOTIFICATION_H_
#define _BDV_NOTIFICATION_H_

#include <memory>

#include "log.h"
#include "bdmenums.h"
#include "Blockchain.h"
#include "LedgerEntry.h"
#include "ZeroConf.h"
#include "nodeRPC.h"

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification
{
private:
   //notificaiton with empty ID means broadcast to all bdv
   const std::string bdvID_;

public:
   BDV_Notification(const std::string& id) :
      bdvID_(id)
   {}

   virtual ~BDV_Notification(void) {}
   virtual BDV_Action action_type(void) = 0;

   const std::string& bdvID(void) const { return bdvID_; }
};

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification_Init : public BDV_Notification
{
   BDV_Notification_Init(void) :
      BDV_Notification("")
   {}

   BDV_Action action_type(void)
   {
      return BDV_Init;
   }
};

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification_NewBlock : public BDV_Notification
{
   Blockchain::ReorganizationState reorgState_;
   std::shared_ptr<ZcPurgePacket> zcPurgePacket_;
   std::shared_ptr<ZeroConfSharedStateSnapshot> zcState_;

   BDV_Notification_NewBlock(
      const Blockchain::ReorganizationState& ref, 
      std::shared_ptr<ZcPurgePacket> purgePacket) :
      BDV_Notification(""),
      reorgState_(ref), zcPurgePacket_(purgePacket)
   {}

   BDV_Action action_type(void)
   {
      return BDV_NewBlock;
   }
};

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification_ZC : public BDV_Notification
{
   const ZeroConfContainer::NotificationPacket packet_;
   std::vector<LedgerEntry> leVec_;

   BDV_Notification_ZC(ZeroConfContainer::NotificationPacket& packet) :
      BDV_Notification(packet.bdvID_), packet_(std::move(packet))
   {}

   BDV_Action action_type(void)
   {
      return BDV_ZC;
   }
};

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification_Refresh : public BDV_Notification
{
   const BDV_refresh refresh_;
   const BinaryData refreshID_;
   ZeroConfContainer::NotificationPacket zcPacket_;

   BDV_Notification_Refresh(const std::string& bdvID,
      BDV_refresh refresh, const BinaryData& refreshID) :
      BDV_Notification(bdvID),
      refresh_(refresh), refreshID_(refreshID), zcPacket_(bdvID)
   {}

   BDV_Action action_type(void)
   {
      return BDV_Refresh;
   }
};

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification_Progress : public BDV_Notification
{
   BDMPhase phase_;
   double progress_;
   unsigned time_;
   unsigned numericProgress_;
   const std::vector<std::string> walletIDs_;

   BDV_Notification_Progress(BDMPhase phase, double prog,
      unsigned time, unsigned numProg, const std::vector<std::string>& walletIDs) :
      BDV_Notification(""),
      phase_(phase), progress_(prog), time_(time),
      numericProgress_(numProg), walletIDs_(walletIDs)
   {}

   BDV_Action action_type(void)
   {
      return BDV_Progress;
   }
};

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification_NodeStatus : public BDV_Notification
{
   const NodeStatusStruct status_;

   BDV_Notification_NodeStatus(NodeStatusStruct nss) :
      BDV_Notification(""), status_(nss)
   {}

   BDV_Action action_type(void)
   {
      return BDV_NodeStatus;
   }
};

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification_Error : public BDV_Notification
{
   BDV_Error_Struct errStruct;

   BDV_Notification_Error(const std::string& bdvID,
      BDV_ErrorType errt, std::string errstr, std::string extra) :
      BDV_Notification(bdvID)
   {
      errStruct.errType_ = errt;
      errStruct.errorStr_ = errstr;
      errStruct.extraMsg_ = extra;
   }

   BDV_Action action_type(void)
   {
      return BDV_Error;
   }

};

class BDV_Server_Object;

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification_Packet
{
   std::shared_ptr<BDV_Server_Object> bdvPtr_;
   std::shared_ptr<BDV_Notification> notifPtr_;
};

#endif