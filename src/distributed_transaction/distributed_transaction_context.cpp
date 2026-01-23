#include "distributed_transaction_types.h"
#include <algorithm>

// DistributedTransactionContext 实现
DistributedTransactionContext::DistributedTransactionContext(
    const std::string& txn_id, const std::string& coordinator_id)
    : transaction_id_(txn_id), coordinator_id_(coordinator_id),
      state_(DistributedTransactionState::ACTIVE),
      start_time_(std::chrono::system_clock::now()),
      timeout_(std::chrono::milliseconds(30000)) {
}

DistributedTransactionContext::~DistributedTransactionContext() {
}

DistributedTransactionState DistributedTransactionContext::get_state() const {
    std::lock_guard<std::mutex> lock(context_mutex_);
    return state_;
}

void DistributedTransactionContext::set_state(DistributedTransactionState state) {
    std::lock_guard<std::mutex> lock(context_mutex_);
    state_ = state;
}

void DistributedTransactionContext::add_participant(const ParticipantInfo& participant) {
    std::lock_guard<std::mutex> lock(context_mutex_);
    
    // 检查是否已存在
    auto it = std::find_if(participants_.begin(), participants_.end(),
                          [&participant](const ParticipantInfo& p) {
                              return p.node_id == participant.node_id;
                          });
    
    if (it == participants_.end()) {
        participants_.push_back(participant);
    } else {
        // 更新现有参与者信息
        *it = participant;
    }
}

void DistributedTransactionContext::update_participant_state(
    const std::string& node_id, ParticipantState state) {
    
    std::lock_guard<std::mutex> lock(context_mutex_);
    
    auto it = std::find_if(participants_.begin(), participants_.end(),
                          [&node_id](ParticipantInfo& p) {
                              return p.node_id == node_id;
                          });
    
    if (it != participants_.end()) {
        it->state = state;
        it->last_contact = std::chrono::system_clock::now();
    }
}

std::vector<ParticipantInfo> DistributedTransactionContext::get_participants() const {
    std::lock_guard<std::mutex> lock(context_mutex_);
    return participants_;
}

ParticipantInfo* DistributedTransactionContext::get_participant(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(context_mutex_);
    
    auto it = std::find_if(participants_.begin(), participants_.end(),
                          [&node_id](const ParticipantInfo& p) {
                              return p.node_id == node_id;
                          });
    
    return (it != participants_.end()) ? &(*it) : nullptr;
}

size_t DistributedTransactionContext::get_participant_count() const {
    std::lock_guard<std::mutex> lock(context_mutex_);
    return participants_.size();
}

void DistributedTransactionContext::add_operation(const DistributedOperation& operation) {
    std::lock_guard<std::mutex> lock(context_mutex_);
    operations_.push_back(operation);
}

std::vector<DistributedOperation> DistributedTransactionContext::get_operations() const {
    std::lock_guard<std::mutex> lock(context_mutex_);
    return operations_;
}

std::vector<DistributedOperation> DistributedTransactionContext::get_operations_for_node(
    const std::string& node_id) const {
    
    std::lock_guard<std::mutex> lock(context_mutex_);
    
    std::vector<DistributedOperation> node_operations;
    for (const auto& op : operations_) {
        if (op.node_id == node_id) {
            node_operations.push_back(op);
        }
    }
    
    return node_operations;
}

bool DistributedTransactionContext::all_participants_prepared() const {
    std::lock_guard<std::mutex> lock(context_mutex_);
    
    for (const auto& participant : participants_) {
        if (participant.state != ParticipantState::PREPARED) {
            return false;
        }
    }
    
    return !participants_.empty();
}

bool DistributedTransactionContext::any_participant_aborted() const {
    std::lock_guard<std::mutex> lock(context_mutex_);
    
    for (const auto& participant : participants_) {
        if (participant.state == ParticipantState::ABORTED || 
            participant.state == ParticipantState::FAILED) {
            return true;
        }
    }
    
    return false;
}

bool DistributedTransactionContext::all_participants_committed() const {
    std::lock_guard<std::mutex> lock(context_mutex_);
    
    for (const auto& participant : participants_) {
        if (participant.state != ParticipantState::COMMITTED) {
            return false;
        }
    }
    
    return !participants_.empty();
}

size_t DistributedTransactionContext::get_prepared_count() const {
    std::lock_guard<std::mutex> lock(context_mutex_);
    
    return std::count_if(participants_.begin(), participants_.end(),
                        [](const ParticipantInfo& p) {
                            return p.state == ParticipantState::PREPARED;
                        });
}

size_t DistributedTransactionContext::get_committed_count() const {
    std::lock_guard<std::mutex> lock(context_mutex_);
    
    return std::count_if(participants_.begin(), participants_.end(),
                        [](const ParticipantInfo& p) {
                            return p.state == ParticipantState::COMMITTED;
                        });
}

size_t DistributedTransactionContext::get_aborted_count() const {
    std::lock_guard<std::mutex> lock(context_mutex_);
    
    return std::count_if(participants_.begin(), participants_.end(),
                        [](const ParticipantInfo& p) {
                            return p.state == ParticipantState::ABORTED ||
                                   p.state == ParticipantState::FAILED;
                        });
}

bool DistributedTransactionContext::is_timeout() const {
    auto now = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_);
    return elapsed >= timeout_;
}