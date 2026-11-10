#include <RE/Skyrim.h>

#include "NativeRead.h"

#include <mutex>

namespace tds {

class PendingRead {
public:
    explicit PendingRead(std::string_view verifiedSha) : verifiedSha_(verifiedSha) {}

    void fail(ReadStatus status, std::string_view detail)
    {
        std::scoped_lock lock(mutex_);
        if (!result_) {
            result_ = Snapshot{status, {}, detail};
        }
    }

    void receiveSkill(std::size_t slot, float value)
    {
        std::scoped_lock lock(mutex_);
        if (!result_ && !values_[slot]) {
            values_[slot] = value;
            ++received_;
            completeIfReady();
        }
    }

    void receiveReady(bool ready)
    {
        std::scoped_lock lock(mutex_);
        if (result_) {
            return;
        }
        if (!ready) {
            result_ = Snapshot{ReadStatus::NotReady, {}, "IS_SKILLED_FALSE"};
            return;
        }
        actorReady_ = true;
        completeIfReady();
    }

    [[nodiscard]] std::optional<Snapshot> result() const
    {
        std::scoped_lock lock(mutex_);
        return result_;
    }

private:
    void completeIfReady()
    {
        if (actorReady_ && received_ == values_.size()) {
            result_ = decodeNativeStats(verifiedSha_, ReadStatus::Valid, values_);
            result_->detail = result_->status == ReadStatus::Valid ? "SCALAR_BATCH" : "INVALID_SCALAR_VALUE";
        }
    }

    std::string verifiedSha_;
    mutable std::mutex mutex_;
    NativeValues values_{};
    std::size_t received_{};
    bool actorReady_{};
    std::optional<Snapshot> result_;
};

namespace {

class ReadCallback final : public RE::BSScript::IStackCallbackFunctor {
public:
    ReadCallback(const std::shared_ptr<PendingRead>& read, std::optional<std::size_t> slot) :
        read_(read), slot_(slot)
    {}

    void operator()(RE::BSScript::Variable result) override
    {
        const auto read = read_.lock();
        if (!read) {
            return;
        }
        if (!slot_) {
            if (result.IsBool()) {
                read->receiveReady(result.GetBool());
            } else {
                read->fail(ReadStatus::Error, "IS_SKILLED_RETURN_NOT_BOOL");
            }
        } else if (result.IsFloat()) {
            read->receiveSkill(*slot_, result.GetFloat());
        } else {
            read->fail(ReadStatus::Error, "GET_SKILL_RETURN_NOT_FLOAT");
        }
    }

    void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

private:
    std::weak_ptr<PendingRead> read_;
    std::optional<std::size_t> slot_;
};

}  // namespace

std::shared_ptr<PendingRead> beginPlayerRead(std::string_view verifiedSha)
{
    auto read = std::make_shared<PendingRead>(verifiedSha);
    if (verifiedSha != frameworkStatsSha && verifiedSha != utilityPlusStatsSha) {
        read->fail(ReadStatus::NotReady, "PEX_UNSUPPORTED");
        return read;
    }
    auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!vm || !player) {
        read->fail(ReadStatus::NotReady, "VM_OR_PLAYER_UNAVAILABLE");
        return read;
    }

    // MCM GetSkill delegates to this read-only scalar native, after SeedActor.
    // TDS checks IsSkilled without seeding or changing the SexLab record.
    RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> readyCallback(new ReadCallback(read, std::nullopt));
    if (!vm->DispatchStaticCall("sslActorStats", "IsSkilled",
            RE::MakeFunctionArguments(static_cast<RE::Actor*>(player)), readyCallback)) {
        read->fail(ReadStatus::Error, "IS_SKILLED_DISPATCH_REJECTED");
        return read;
    }
    for (std::size_t slot = 0; slot < skillIndices.size(); ++slot) {
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new ReadCallback(read, slot));
        if (!vm->DispatchStaticCall("sslActorStats", "_GetSkill",
                RE::MakeFunctionArguments(static_cast<RE::Actor*>(player), int{skillIndices[slot]}), callback)) {
            read->fail(ReadStatus::Error, "GET_SKILL_DISPATCH_REJECTED");
            break;
        }
    }
    return read;
}

std::optional<Snapshot> completedRead(const std::shared_ptr<PendingRead>& read)
{
    return read->result();
}

}  // namespace tds
