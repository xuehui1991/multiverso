#include "multiverso/communicator.h"

#include <memory>

#include "multiverso/zoo.h"
#include "multiverso/net.h"
#include "multiverso/util/log.h"
#include "multiverso/util/mt_queue.h"

namespace multiverso {

namespace message {
// TODO(feiga): refator the ugly statement
bool to_server(MsgType type) {
  return (static_cast<int>(type)) > 0 &&
         (static_cast<int>(type)) < 32;
}

bool to_worker(MsgType type) {
  return (static_cast<int>(type)) < 0  &&
         (static_cast<int>(type)) > -32;
}

bool to_controler(MsgType type) {
  return (static_cast<int>(type)) > 32;
}

}  // namespace message

Communicator::Communicator() : Actor(actor::kCommunicator) {
  RegisterHandler(MsgType::Default, std::bind(
    &Communicator::ProcessMessage, this, std::placeholders::_1));
  net_util_ = NetInterface::Get();
}

Communicator::~Communicator() {
  if (recv_thread_.get() && recv_thread_->joinable())
    recv_thread_->join();
}

void Communicator::Main() {
  is_working_ = true;
  // TODO(feiga): join the thread, make sure it exit properly
  switch (net_util_->thread_level_support()) {
  case NetThreadLevel::THREAD_MULTIPLE: {
    recv_thread_.reset(new std::thread(&Communicator::Communicate, this));
    Actor::Main();
    break;
  }
  case NetThreadLevel::THREAD_SERIALIZED: {
    MessagePtr msg;
    while (mailbox_->Alive()) {
      // Try pop and Send
      if (mailbox_->TryPop(msg)) {
        ProcessMessage(msg);
      }
      // Probe and Recv
      size_t size = net_util_->Recv(&msg);
      if (size > 0) LocalForward(msg);
      CHECK(msg.get() == nullptr);
      net_util_->Send(msg);
    }
    break;
  }
  default:
    Log::Fatal("Unexpected thread level\n");
  }
}

void Communicator::ProcessMessage(MessagePtr& msg) {
  if (msg->dst() != net_util_->rank()) {
    net_util_->Send(msg);
    return;
  }
  LocalForward(msg);
}

void Communicator::Communicate() {
  while (net_util_->active()) {
    MessagePtr msg(new Message());
    size_t size = net_util_->Recv(&msg);
    if (size == -1) {
      Log::Debug("recv return -1\n");
      return;
    }
    if (size > 0) {
      // a message received
      CHECK(msg->dst() == Zoo::Get()->rank());
      LocalForward(msg);
    }
  }
  Log::Info("Comm recv thread exit\n");
}

void Communicator::LocalForward(MessagePtr& msg) {
  CHECK(msg->dst() == Zoo::Get()->rank());
  if (message::to_server(msg->type())) {
    SendTo(actor::kServer, msg);
  } else if (message::to_worker(msg->type())) {
    SendTo(actor::kWorker, msg);
  } else if (message::to_controler(msg->type())) {
    SendTo(actor::kController, msg);
  } else {
    // Send back to the msg queue of zoo
    Zoo::Get()->Receive(msg);
  }
}

}  // namespace multiverso
