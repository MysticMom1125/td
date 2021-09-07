//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SponsoredMessageManager.h"

#include "td/telegram/ChannelId.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageContent.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/net/NetQueryCreator.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/Status.h"

namespace td {

class GetSponsoredMessagesQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::messages_sponsoredMessages>> promise_;
  ChannelId channel_id_;

 public:
  explicit GetSponsoredMessagesQuery(
      Promise<telegram_api::object_ptr<telegram_api::messages_sponsoredMessages>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id) {
    channel_id_ = channel_id;
    auto input_channel = td->contacts_manager_->get_input_channel(channel_id);
    if (input_channel == nullptr) {
      return promise_.set_error(Status::Error(3, "Chat info not found"));
    }
    send_query(G()->net_query_creator().create(telegram_api::channels_getSponsoredMessages(std::move(input_channel))));
  }

  void on_result(uint64 id, BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::channels_getSponsoredMessages>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    promise_.set_value(result_ptr.move_as_ok());
  }

  void on_error(uint64 id, Status status) final {
    td->contacts_manager_->on_get_channel_error(channel_id_, status, "GetSponsoredMessagesQuery");
    promise_.set_error(std::move(status));
  }
};

class ViewSponsoredMessageQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;

 public:
  explicit ViewSponsoredMessageQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, const string &message_id) {
    channel_id_ = channel_id;
    auto input_channel = td->contacts_manager_->get_input_channel(channel_id);
    if (input_channel == nullptr) {
      return promise_.set_error(Status::Error(3, "Chat info not found"));
    }
    send_query(G()->net_query_creator().create(
        telegram_api::channels_viewSponsoredMessage(std::move(input_channel), BufferSlice(message_id))));
  }

  void on_result(uint64 id, BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::channels_viewSponsoredMessage>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) final {
    td->contacts_manager_->on_get_channel_error(channel_id_, status, "ViewSponsoredMessageQuery");
    promise_.set_error(std::move(status));
  }
};

struct SponsoredMessageManager::SponsoredMessage {
  string random_id;
  DialogId sponsor_dialog_id;
  string start_param;
  unique_ptr<MessageContent> content;

  SponsoredMessage() = default;
  SponsoredMessage(string random_id, DialogId sponsor_dialog_id, string start_param, unique_ptr<MessageContent> content)
      : random_id(std::move(random_id))
      , sponsor_dialog_id(sponsor_dialog_id)
      , start_param(std::move(start_param))
      , content(std::move(content)) {
  }
};

struct SponsoredMessageManager::DialogSponsoredMessages {
  vector<Promise<td_api::object_ptr<td_api::sponsoredMessages>>> promises;
  vector<SponsoredMessage> messages;
};

SponsoredMessageManager::SponsoredMessageManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

SponsoredMessageManager::~SponsoredMessageManager() = default;

void SponsoredMessageManager::tear_down() {
  parent_.reset();
}

td_api::object_ptr<td_api::sponsoredMessage> SponsoredMessageManager::get_sponsored_message_object(
    DialogId dialog_id, const SponsoredMessage &sponsored_message) const {
  return td_api::make_object<td_api::sponsoredMessage>(
      sponsored_message.random_id, sponsored_message.sponsor_dialog_id.get(), sponsored_message.start_param,
      get_message_content_object(sponsored_message.content.get(), td_, dialog_id, 0, false, true, -1));
}

td_api::object_ptr<td_api::sponsoredMessages> SponsoredMessageManager::get_sponsored_messages_object(
    DialogId dialog_id, const DialogSponsoredMessages &sponsored_messages) const {
  return td_api::make_object<td_api::sponsoredMessages>(
      transform(sponsored_messages.messages, [this, dialog_id](const SponsoredMessage &sponsored_message) {
        return get_sponsored_message_object(dialog_id, sponsored_message);
      }));
}

void SponsoredMessageManager::get_dialog_sponsored_messages(
    DialogId dialog_id, Promise<td_api::object_ptr<td_api::sponsoredMessages>> &&promise) {
  if (!td_->messages_manager_->have_dialog_force(dialog_id, "get_sponsored_messages")) {
    return promise.set_error(Status::Error(400, "Chat not found"));
  }
  if (dialog_id.get_type() != DialogType::Channel ||
      td_->contacts_manager_->get_channel_type(dialog_id.get_channel_id()) != ContactsManager::ChannelType::Broadcast) {
    return promise.set_value(td_api::make_object<td_api::sponsoredMessages>());
  }

  auto &messages = dialog_sponsored_messages_[dialog_id];
  if (messages == nullptr) {
    messages = make_unique<DialogSponsoredMessages>();
  }
  messages->promises.push_back(std::move(promise));
  if (messages->promises.size() == 1) {
    auto query_promise = PromiseCreator::lambda(
        [actor_id = actor_id(this),
         dialog_id](Result<telegram_api::object_ptr<telegram_api::messages_sponsoredMessages>> &&result) mutable {
          send_closure(actor_id, &SponsoredMessageManager::on_get_dialog_sponsored_messages, dialog_id,
                       std::move(result));
        });
    td_->create_handler<GetSponsoredMessagesQuery>(std::move(query_promise))->send(dialog_id.get_channel_id());
  }
}

void SponsoredMessageManager::on_get_dialog_sponsored_messages(
    DialogId dialog_id, Result<telegram_api::object_ptr<telegram_api::messages_sponsoredMessages>> &&result) {
  auto &messages = dialog_sponsored_messages_[dialog_id];
  CHECK(messages != nullptr);
  auto promises = std::move(messages->promises);
  reset_to_empty(messages->promises);

  if (result.is_ok() && G()->close_flag()) {
    result = Status::Error(500, "Request aborted");
  }
  if (result.is_error()) {
    for (auto &promise : promises) {
      promise.set_error(result.error().clone());
    }
    return;
  }

  auto sponsored_messages = result.move_as_ok();

  td_->contacts_manager_->on_get_users(std::move(sponsored_messages->users_), "on_get_dialog_sponsored_messages");
  td_->contacts_manager_->on_get_chats(std::move(sponsored_messages->chats_), "on_get_dialog_sponsored_messages");

  reset_to_empty(messages->messages);
  for (auto &sponsored_message : sponsored_messages->messages_) {
    DialogId sponsor_dialog_id(sponsored_message->from_id_);
    if (!sponsor_dialog_id.is_valid() || !td_->messages_manager_->have_dialog_info_force(sponsor_dialog_id)) {
      LOG(ERROR) << "Receive unknown sponsor " << sponsor_dialog_id;
      continue;
    }
    td_->messages_manager_->force_create_dialog(sponsor_dialog_id, "on_get_dialog_sponsored_messages");
    auto message_text = get_message_text(td_->contacts_manager_.get(), std::move(sponsored_message->message_),
                                         std::move(sponsored_message->entities_), true, true, 0, false,
                                         "on_get_dialog_sponsored_messages");
    int32 ttl = 0;
    auto content = get_message_content(td_, std::move(message_text), nullptr, sponsor_dialog_id, true, UserId(), &ttl);
    if (ttl != 0) {
      LOG(ERROR) << "Receive sponsored message with TTL " << ttl;
      continue;
    }

    messages->messages.emplace_back(sponsored_message->random_id_.as_slice().str(), sponsor_dialog_id,
                                    std::move(sponsored_message->start_param_), std::move(content));
  }

  for (auto &promise : promises) {
    promise.set_value(get_sponsored_messages_object(dialog_id, *messages));
  }
}

void SponsoredMessageManager::view_sponsored_message(DialogId dialog_id, const string &message_id,
                                                     Promise<Unit> &&promise) {
  if (!td_->messages_manager_->have_dialog_force(dialog_id, "view_sponsored_message")) {
    return promise.set_error(Status::Error(400, "Chat not found"));
  }
  if (dialog_id.get_type() != DialogType::Channel ||
      td_->contacts_manager_->get_channel_type(dialog_id.get_channel_id()) != ContactsManager::ChannelType::Broadcast ||
      message_id.empty()) {
    return promise.set_error(Status::Error(400, "Message not found"));
  }

  td_->create_handler<ViewSponsoredMessageQuery>(std::move(promise))->send(dialog_id.get_channel_id(), message_id);
}

}  // namespace td
