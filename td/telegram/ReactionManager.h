//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ChatReactions.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/ReactionListType.h"
#include "td/telegram/ReactionType.h"
#include "td/telegram/ReactionUnavailabilityReason.h"
#include "td/telegram/SavedMessagesTopicId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

#include <utility>

namespace td {

class Td;

class ReactionManager final : public Actor {
 public:
  ReactionManager(Td *td, ActorShared<> parent);
  ReactionManager(const ReactionManager &) = delete;
  ReactionManager &operator=(const ReactionManager &) = delete;
  ReactionManager(ReactionManager &&) = delete;
  ReactionManager &operator=(ReactionManager &&) = delete;
  ~ReactionManager() final;

  static constexpr size_t MAX_RECENT_REACTIONS = 100;  // some reasonable value

  void init();

  bool is_active_reaction(const ReactionType &reaction_type) const;

  void get_emoji_reaction(const string &emoji, Promise<td_api::object_ptr<td_api::emojiReaction>> &&promise);

  td_api::object_ptr<td_api::availableReactions> get_sorted_available_reactions(
      ChatReactions available_reactions, ChatReactions active_reactions, int32 row_size, bool is_tag,
      ReactionUnavailabilityReason unavailability_reason);

  td_api::object_ptr<td_api::availableReactions> get_available_reactions(int32 row_size);

  void add_recent_reaction(const ReactionType &reaction_type);

  void clear_recent_reactions(Promise<Unit> &&promise);

  vector<ReactionType> get_default_tag_reactions();

  void reload_reactions();

  void reload_reaction_list(ReactionListType reaction_list_type);

  void on_get_reaction_list(ReactionListType reaction_list_type,
                            tl_object_ptr<telegram_api::messages_Reactions> &&reactions_ptr);

  void on_get_available_reactions(tl_object_ptr<telegram_api::messages_AvailableReactions> &&available_reactions_ptr);

  void set_default_reaction(ReactionType reaction_type, Promise<Unit> &&promise);

  void send_set_default_reaction_query();

  void get_saved_messages_tags(SavedMessagesTopicId saved_messages_topic_id,
                               Promise<td_api::object_ptr<td_api::savedMessagesTags>> &&promise);

  void on_update_saved_reaction_tags(Promise<Unit> &&promise);

  void update_saved_messages_tags(SavedMessagesTopicId saved_messages_topic_id, const vector<ReactionType> &old_tags,
                                  const vector<ReactionType> &new_tags);

  void set_saved_messages_tag_title(ReactionType reaction_type, string title, Promise<Unit> &&promise);

  void get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const;

 private:
  struct Reaction {
    ReactionType reaction_type_;
    string title_;
    bool is_active_ = false;
    bool is_premium_ = false;
    FileId static_icon_;
    FileId appear_animation_;
    FileId select_animation_;
    FileId activate_animation_;
    FileId effect_animation_;
    FileId around_animation_;
    FileId center_animation_;

    bool is_valid() const {
      return static_icon_.is_valid() && appear_animation_.is_valid() && select_animation_.is_valid() &&
             activate_animation_.is_valid() && effect_animation_.is_valid() && !reaction_type_.is_empty();
    }

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  struct Reactions {
    int32 hash_ = 0;
    bool are_being_reloaded_ = false;
    vector<Reaction> reactions_;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  struct ReactionList {
    int64 hash_ = 0;
    bool is_loaded_from_database_ = false;
    bool is_being_reloaded_ = false;
    vector<ReactionType> reaction_types_;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  static constexpr int32 MAX_TAG_TITLE_LENGTH = 12;

  struct SavedReactionTag {
    ReactionType reaction_type_;
    uint64 hash_ = 0;
    string title_;
    int32 count_ = 0;

    SavedReactionTag() = default;

    explicit SavedReactionTag(telegram_api::object_ptr<telegram_api::savedReactionTag> &&tag);

    SavedReactionTag(const ReactionType &reaction_type, const string &title, int32 count);

    bool is_valid() const {
      return !reaction_type_.is_empty() && count_ >= 0 && (count_ > 0 || !title_.empty());
    }

    td_api::object_ptr<td_api::savedMessagesTag> get_saved_messages_tag_object() const;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  friend bool operator==(const SavedReactionTag &lhs, const SavedReactionTag &rhs);

  friend bool operator!=(const SavedReactionTag &lhs, const SavedReactionTag &rhs);

  friend bool operator<(const SavedReactionTag &lhs, const SavedReactionTag &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const SavedReactionTag &saved_reaction_tag);

  struct SavedReactionTags {
    vector<SavedReactionTag> tags_;
    int64 hash_ = 0;
    bool is_inited_ = false;

    td_api::object_ptr<td_api::savedMessagesTags> get_saved_messages_tags_object() const;

    bool update_saved_messages_tags(const vector<ReactionType> &old_tags, const vector<ReactionType> &new_tags);

    bool set_tag_title(const ReactionType &reaction_type, const string &title);

    int64 calc_hash() const;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  td_api::object_ptr<td_api::emojiReaction> get_emoji_reaction_object(const string &emoji) const;

  ReactionList &get_reaction_list(ReactionListType reaction_list_type);

  void start_up() final;

  void tear_down() final;

  void save_active_reactions();

  void save_reactions();

  void save_reaction_list(ReactionListType reaction_list_type);

  void load_active_reactions();

  void load_reactions();

  void load_reaction_list(ReactionListType reaction_list_type);

  void update_active_reactions();

  td_api::object_ptr<td_api::updateActiveEmojiReactions> get_update_active_emoji_reactions_object() const;

  SavedReactionTags *get_saved_reaction_tags(SavedMessagesTopicId saved_messages_topic_id);

  void reget_saved_messages_tags(SavedMessagesTopicId saved_messages_topic_id,
                                 Promise<td_api::object_ptr<td_api::savedMessagesTags>> &&promise);

  void on_get_saved_messages_tags(SavedMessagesTopicId saved_messages_topic_id,
                                  Result<telegram_api::object_ptr<telegram_api::messages_SavedReactionTags>> &&r_tags);

  string get_saved_messages_tags_database_key(SavedMessagesTopicId saved_messages_topic_id);

  void load_all_saved_reaction_tags_from_database();

  td_api::object_ptr<td_api::updateSavedMessagesTags> get_update_saved_messages_tags_object(
      SavedMessagesTopicId saved_messages_topic_id, const SavedReactionTags *tags) const;

  void send_update_saved_messages_tags(SavedMessagesTopicId saved_messages_topic_id, const SavedReactionTags *tags,
                                       bool from_database = false);

  Td *td_;
  ActorShared<> parent_;

  bool is_inited_ = false;
  bool are_reactions_loaded_from_database_ = false;
  bool are_all_tags_loaded_from_database_ = false;

  vector<std::pair<string, Promise<td_api::object_ptr<td_api::emojiReaction>>>> pending_get_emoji_reaction_queries_;

  Reactions reactions_;
  vector<ReactionType> active_reaction_types_;

  ReactionList reaction_lists_[MAX_REACTION_LIST_TYPE];

  SavedReactionTags all_tags_;
  FlatHashMap<SavedMessagesTopicId, unique_ptr<SavedReactionTags>, SavedMessagesTopicIdHash> topic_tags_;

  vector<Promise<td_api::object_ptr<td_api::savedMessagesTags>>> pending_get_all_saved_reaction_tags_queries_;
  FlatHashMap<SavedMessagesTopicId, vector<Promise<td_api::object_ptr<td_api::savedMessagesTags>>>,
              SavedMessagesTopicIdHash>
      pending_get_topic_saved_reaction_tags_queries_;
};

}  // namespace td
