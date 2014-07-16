// Copyright © 2014 Anh Dinh. All Rights Reserved.

// derived piccolo/rpc.h, for the definition of
// NetworkThread, and RPCRequest representing the message
// sent over the network. NetworkThread thread is started at every
// MPI process when the system starts up

// incoming messages are put into MessageQueue and processed by a
// MessageProcessingThread.

// global flag sync_update (default = false) switches the concurrency model
// in synchronous mode, for each key,  all N updates (N = # machines) must be
// processed before the gets, then all N gets are processed before any update.
//
// in synhronous mode, for each key requests are processed in FIFO fashion.

#ifndef INCLUDE_CORE_RPC_H_
#define INCLUDE_CORE_RPC_H_

#include <boost/thread.hpp>
#include <boost/function.hpp>
#include <google/protobuf/message.h>
#include <mpi.h>

#include "core/common.h"
#include "core/file.h"

DECLARE_bool(sync_update);

namespace lapis {

typedef google::protobuf::Message Message;

// Wrapper of the network message
struct RPCRequest;

struct TaggedMessage;

class RequestQueue;

// Hackery to get around mpi's unhappiness with threads.  This thread
// simply polls MPI continuously for any kind of update and adds it to
// a local queue.
class NetworkThread {
 public:

  // Blocking read for the given source and message type.
  void Read(int desired_src, int type, Message* data, int *source = NULL);
  bool TryRead(int desired_src, int type, Message* data, int *source = NULL);

  // Enqueue the given request for transmission.
  void Send(RPCRequest *req);
  void Send(int dst, int method, const Message &msg);

  void Broadcast(int method, const Message& msg);
  void SyncBroadcast(int method, int reply, const Message& msg);

  RPCRequest *NextRequest(){return request_queue_->NextRequest();}

  void Flush();
  void Shutdown();

  int id() { return id_; }
  int size() const { return world_->Get_size(); }

  static NetworkThread *Get(){ return net_;}

  static void Init();

  // callback to function (in worker.cc) to handle top-priority message
  // such as shard assignment, etc.
  typedef boost::function<void ()> Callback;

  //  callback to handle put/get requests
  typedef boost::function<void (const Message*)> Handle;

  void RegisterCallback(int message_type, Callback cb) {callbacks_[message_type] = cb;}
  void RegisterRequestHandler(int message_type, Handle cb) {handles_[message_type] = cb;}

 private:
  static const int kMaxHosts = 512;
  static const int kMaxMethods = 36;

  typedef deque<string> Queue;

  static NetworkThread* net_;

  //  set to false when receiving MTYPE_WORKER_SHUTDOWN from the manager
  //  shared by the receiving thread and processing thread.
  volatile bool running_;

  Callback callbacks_[kMaxMethods];
  Handle handles_[kMaxMethods];

  //queues of sent messages
  vector<RPCRequest*> pending_sends_;
  unordered_set<RPCRequest*> active_sends_;

  int id_;
  MPI::Comm *world_;

  //send lock
  mutable boost::recursive_mutex send_lock;
  //received locks, one for each kMaxHosts
  boost::recursive_mutex response_queue_locks_[kMaxMethods];

  mutable boost::thread* sender_and_reciever_thread_, processing_thread_;


  //request (put/get) queue
  RequestQueue* request_queue_;

  //response queue (read)
  Queue response_queue_[kMaxMethods][kMaxHosts];

  bool check_queue(int src, int type, Message* data);

  //  if there're still messages to send
  bool is_active() const;

  //  reclaim memory of the send queue
  void CollectActive();

  //  loop that receives and sends messages
  void NetworkLoop();

  //  loop that processes received messages
  void ProcessLoop();


  //  helper for ProcessLoop();
  void ProcessRequest(const TaggedMessage& t_msg);

  void WaitForSync(int method, int count);

  NetworkThread();
};

class RequestQueue{
 public:
  MessageQueue(int num_keys, int ns): num_keys_(num_keys), num_mem_servers_(ns), key_index_(0), is_first_update_(true) {}

  virtual void NextRequest(TaggedMessage* msg)=0;
  virtual void Enqueue(int tag, string data)=0;
 private:
  void ExtractKey(int tag, string data, string* key);

  typedef deque<TaggedMessage> Queue;
  typedef vector<boost::recursive_mutex> Lock;

  map<string, int> key_map_;
  //mapping key(string) to lock
  Lock key_locks_;

  boost::recursive_mutex whole_queue_lock_;

  bool is_first_update_;

  int num_keys_;
  int num_mem_servers_;
  int key_index_;
};

//  synchronous queue
class SyncRequestQueue: public RequestQueue{
 public:
  SyncMessageQueue(int num_keys, int ns): MessageQueue(num_keys, ns){}
  void NextRequest(TaggedMessage* msg);
  void Enqueue(int tag, string& data);
 private:
  vector<Queue> request_queues_;
};

//  asynchronous queue
class AsyncRequestQueue: public RequestQueue{
 public:
  AsyncMessageQueue(int num_keys, int ns): RequestQueue(num_keys, ns) {}
  void NextRequest(TaggedMessage* msg);
  void Enqueue(int tag, string& data);
 private:
  vector<Queue> put_queues_, get_queues_;
  vector<int> access_counters_;
  vector<bool> is_in_put_queue_;
};
}  // namespace lapis

#endif  // INCLUDE_CORE_RPC_H_

