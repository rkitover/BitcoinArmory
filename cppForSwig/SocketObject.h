////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016, goatpig.                                              //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                      
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _SOCKETOBJ_H
#define _SOCKETOBJ_H

#include <sys/types.h>
#include <string>
#include <sstream>
#include <stdint.h>
#include <functional>
#include <memory>

#ifndef _WIN32
#include <poll.h>
#define socketService socketService_nix
#else
#define socketService socketService_win
#endif

#include "ThreadSafeClasses.h"
#include "bdmenums.h"
#include "log.h"
#include "SocketIncludes.h"
#include "BinaryData.h"

#include <google/protobuf/message.h>
   
typedef std::function<bool(std::vector<uint8_t>, std::exception_ptr)>  ReadCallback;

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn
{
   virtual ~CallbackReturn(void) = 0;
   virtual void callback(BinaryDataRef bdr) = 0;
};

struct CallbackReturn_CloseBitcoinP2PSocket : public CallbackReturn
{
private:
   std::shared_ptr<BlockingQueue<std::vector<uint8_t>>> dataStack_;

public:
   CallbackReturn_CloseBitcoinP2PSocket(
      std::shared_ptr<BlockingQueue<std::vector<uint8_t>>> datastack) :
      dataStack_(datastack)
   {}

   void callback(const BinaryDataRef& bdr) 
   { dataStack_->terminate(nullptr); }
};

///////////////////////////////////////////////////////////////////////////////
struct Socket_ReadPayload
{
   uint16_t id_ = UINT16_MAX;
   std::unique_ptr<CallbackReturn> callbackReturn_ = nullptr;

   Socket_ReadPayload(void)
   {}

   Socket_ReadPayload(unsigned id) :
      id_(id)
   {}
};

///////////////////////////////////////////////////////////////////////////////
struct Socket_WritePayload
{
   virtual ~Socket_WritePayload(void) = 0;
   virtual void serialize(std::vector<uint8_t>&) = 0;
   virtual std::string serializeToText(void) = 0;
};

////
struct WritePayload_Protobuf : public Socket_WritePayload
{
   std::unique_ptr<::google::protobuf::Message> message_;

   void serialize(std::vector<uint8_t>&);
   std::string serializeToText(void);
};

////
struct WritePayload_Raw : public Socket_WritePayload
{
   std::vector<uint8_t> data_;

   void serialize(std::vector<uint8_t>&);
   std::string serializeToText(void) {
      throw SocketError("raw payload cannot serilaize to str"); }
};

////
struct WritePayload_String : public Socket_WritePayload
{
   std::string data_;

   void serialize(std::vector<uint8_t>&) {
      throw SocketError("string payload cannot serilaize to raw binary");
   }
   
   std::string serializeToText(void) {
      return std::move(data_);
   }
};

////
struct WritePayload_StringPassthrough : public Socket_WritePayload
{
   std::string data_;

   void serialize(std::vector<uint8_t>& payload) {
      payload.reserve(data_.size() +1);
      payload.insert(payload.end(), data_.begin(), data_.end());
      data_.push_back(0);
   }

   std::string serializeToText(void) {
      return move(data_);
   }
};


///////////////////////////////////////////////////////////////////////////////
struct AcceptStruct
{
   SOCKET sockfd_;
   sockaddr saddr_;
   socklen_t addrlen_;
   ReadCallback readCallback_;

   AcceptStruct(void) :
      addrlen_(sizeof(saddr_))
   {}
};

///////////////////////////////////////////////////////////////////////////////
class SocketPrototype
{
   friend class ListenServer;

private: 
   bool blocking_ = true;

protected:

public:
   typedef std::function<bool(const std::vector<uint8_t>&)>  SequentialReadCallback;
   typedef std::function<void(AcceptStruct)> AcceptCallback;

protected:
   const size_t maxread_ = 4*1024*1024;
   
   struct sockaddr serv_addr_;
   const std::string addr_;
   const std::string port_;

   bool verbose_ = true;

private:
   void init(void);

protected:   
   void setBlocking(SOCKET, bool);
   void listen(AcceptCallback, SOCKET& sockfd);

   SocketPrototype(void) :
      addr_(""), port_("")
   {}
   
public:
   SocketPrototype(const std::string& addr, const std::string& port, bool init = true);
   virtual ~SocketPrototype(void) = 0;

   virtual bool testConnection(void);
   bool isBlocking(void) const { return blocking_; }
   SOCKET openSocket(bool blocking);
   
   static void closeSocket(SOCKET&);
   virtual void pushPayload(
      std::unique_ptr<Socket_WritePayload>,
      std::shared_ptr<Socket_ReadPayload>) = 0;
   virtual bool connectToRemote(void) = 0;

   virtual SocketType type(void) const = 0;
   const std::string& getAddrStr(void) const { return addr_; }
   const std::string& getPortStr(void) const { return port_; }
};

///////////////////////////////////////////////////////////////////////////////
class SimpleSocket : public SocketPrototype
{
protected:
   SOCKET sockfd_ = SOCK_MAX;

private:
   int writeToSocket(std::vector<uint8_t>&);

public:
   SimpleSocket(const std::string& addr, const std::string& port) :
      SocketPrototype(addr, port)
   {}
   
   SimpleSocket(SOCKET sockfd) :
      SocketPrototype(), sockfd_(sockfd)
   {}

   ~SimpleSocket(void)
   {
      closeSocket(sockfd_);
   }

   SocketType type(void) const { return SocketSimple; }

   void pushPayload(
      std::unique_ptr<Socket_WritePayload>,
      std::shared_ptr<Socket_ReadPayload>);
   std::vector<uint8_t> readFromSocket(void);
   void shutdown(void);
   void listen(AcceptCallback);
   bool connectToRemote(void);
   SOCKET getSockFD(void) const { return sockfd_; }

   //
   static bool checkSocket(const std::string& ip, const std::string& port);
};

///////////////////////////////////////////////////////////////////////////////
class PersistentSocket : public SocketPrototype
{
   friend class ListenServer;

private:
   SOCKET sockfd_ = SOCK_MAX;
   std::vector<std::thread> threads_;
   
   std::vector<uint8_t> writeLeftOver_;
   size_t writeOffset_ = 0;

   std::atomic<bool> run_;

#ifdef _WIN32
   WSAEVENT events_[2];
#else
   SOCKET pipes_[2];
#endif

   BlockingQueue<std::vector<uint8_t>> readQueue_;
   Queue<std::vector<uint8_t>> writeQueue_;

private:
   void signalService(uint8_t);
#ifdef _WIN32
   void socketService_win(void);
#else
   void socketService_nix(void);
#endif
   void readService(void);
   void initPipes(void);
   void cleanUpPipes(void);
   void init(void);

protected:
   virtual bool processPacket(std::vector<uint8_t>&, std::vector<uint8_t>&);
   virtual void respond(std::vector<uint8_t>&) = 0;
   void queuePayloadForWrite(std::vector<uint8_t>&);

public:
   PersistentSocket(const std::string& addr, const std::string& port) :
      SocketPrototype(addr, port)
   {
      init();
   }

   PersistentSocket(SOCKET sockfd) :
      SocketPrototype(), sockfd_(sockfd)
   {
      init();
   }

   ~PersistentSocket(void)
   {
      shutdown();
   }

   void shutdown();
   bool openSocket(bool blocking);
   int getSocketName(struct sockaddr& sa);
   int getPeerName(struct sockaddr& sa);
   bool connectToRemote(void);
   bool isValid(void) const { return sockfd_ != SOCK_MAX; }
   bool testConnection(void) { return isValid(); }
};

///////////////////////////////////////////////////////////////////////////////
class ListenServer
{
private:
   struct SocketStruct
   {
   private:
      SocketStruct(const SocketStruct&) = delete;

   public:
      SocketStruct(void)
      {}

      std::shared_ptr<SimpleSocket> sock_;
      std::thread thr_;
   };

private:
   std::unique_ptr<SimpleSocket> listenSocket_;
   std::map<SOCKET, std::unique_ptr<SocketStruct>> acceptMap_;
   Queue<SOCKET> cleanUpStack_;

   std::thread listenThread_;
   std::mutex mu_;

private:
   void listenThread(ReadCallback);
   void acceptProcess(AcceptStruct);
   ListenServer(const ListenServer&) = delete;

public:
   ListenServer(const std::string& addr, const std::string& port);
   ~ListenServer(void)
   {
      stop();
   }

   void start(ReadCallback);
   void stop(void);
   void join(void);
};

#endif
