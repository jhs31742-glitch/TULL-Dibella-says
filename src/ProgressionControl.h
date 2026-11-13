#pragma once

#include "SceneRules.h"

#include <limits>
#include <utility>

namespace tds::progression {

enum class Incident : int { Empty, CounterReset, GxpFault, GxpRecovered };

struct ProgressionState {
    std::optional<Counters> lastValid;
    std::array<Incident,3> history{};
    std::optional<Counters> stats;
    Investment nodes;
    SatisfactionBlocks satisfaction;
    std::array<std::optional<SceneAward>,3> recentBlocks{};
    std::int64_t corruption{};
    int soloProgress{};
};

struct RealReadTicket {
    std::uint64_t saveGeneration{};
};

// Single game-thread owner. Worker callbacks post their ticket and result back to that owner.
class ProgressionControl {
public:
    [[nodiscard]] bool developerMode() const { return developer_; }
    [[nodiscard]] std::optional<int> effectiveCount(std::optional<int> verifiedLiveCount) const
    {
        if (!developer_) return verifiedLiveCount;
        return mock_.stats ? std::optional{(*mock_.stats)[N]} : std::nullopt;
    }

    [[nodiscard]] std::optional<RealReadTicket> beginRealRead() const
    {
        if (developer_) return std::nullopt;
        return RealReadTicket{saveGeneration_};
    }

    bool acceptRealRead(RealReadTicket ticket, const Counters& sample)
    {
        if (developer_ || ticket.saveGeneration != saveGeneration_ || !valid(sample)) return false;
        observe(live_,sample);
        return true;
    }

    [[nodiscard]] bool sceneUnchanged(const Counters& sample) const
    {
        return settled_ && *settled_ == sample;
    }

    bool settleScene(double gameHours, bool attributable)
    {
        if (developer_ || !live_.stats) return false;
        const auto before = std::exchange(settled_, live_.stats);
        return attributable && before && *before != *live_.stats && grantScene(live_,*before,gameHours);
    }

private:
    static bool grantScene(ProgressionState& state, const Counters& before, double gameHours)
    {
        if (!state.stats || !std::isfinite(gameHours)) return false;
        auto award = sceneAward(before,*state.stats,state.corruption,state.soloProgress,state.nodes);
        if (!award) return false;
        state.satisfaction.expire(gameHours);
        const int previous=appliedSatisfaction(state.satisfaction.total());
        const bool granted=state.satisfaction.grant(*award,gameHours);
        award->actualBlock=granted ? award->satisfaction : 0;
        award->effectiveChange=appliedSatisfaction(state.satisfaction.total())-previous;
        state.soloProgress=award->soloProgress;
        award->corruption=std::min(award->corruption,
            std::numeric_limits<std::int64_t>::max()-state.corruption);
        state.corruption+=award->corruption;
        std::move_backward(state.recentBlocks.begin(),state.recentBlocks.end()-1,state.recentBlocks.end());
        state.recentBlocks.front()=*award;
        return true;
    }

public:
    [[nodiscard]] double clockHours(double gameHours) const { return developer_ ? mockHours_ : gameHours; }
    void expire(double gameHours)
    {
        if (!developer_) {
            live_.satisfaction.expire(gameHours);
        } else if (std::isfinite(gameHours)) {
            if (mockCalendarHours_ && gameHours > *mockCalendarHours_) {
                mockHours_ += gameHours - *mockCalendarHours_;
            }
            mockCalendarHours_=gameHours;
            mock_.satisfaction.expire(mockHours_);
        }
    }

    void enterDeveloperMode()
    {
        if (developer_) return;
        mock_.stats = mock_.lastValid = Counters{};
        developer_ = true;
        ++saveGeneration_;
    }

    bool resetMockStatistics() {
        if (!developer_) return false;
        setMockCounters(Counters{});
        return resetAll();
    }

    bool setMockCounters(const Counters& sample)
    {
        if (!developer_ || !valid(sample) || sample[S]>500) return false;
        observe(mock_,sample);
        return true;
    }

    bool mockScene(const Counters& increments)
    {
        if (!developer_ || !valid(increments) || !mock_.stats || increments==Counters{}) return false;
        const auto before=*mock_.stats;
        auto sample=before;
        for (std::size_t i=0; i<sample.size(); ++i) {
            if (increments[i]>std::numeric_limits<int>::max()-sample[i]) return false;
            sample[i]+=increments[i];
        }
        if (!setMockCounters(sample)) return false;
        return grantScene(mock_,before,mockHours_);
    }

    bool setMockSatisfaction(int value)
    {
        if (!developer_ || value < -100 || value > 100) return false;
        SatisfactionBlocks blocks;
        if (value && !blocks.grant(SceneAward{value,0,0,8},mockHours_)) return false;
        mock_.satisfaction=std::move(blocks);
        return true;
    }

    bool setMockCorruption(std::int64_t value)
    {
        if (!developer_ || value<0) return false;
        mock_.corruption=value;
        return true;
    }

    bool advanceMockHours(int hours)
    {
        if (!developer_ || hours<=0) return false;
        mockHours_+=hours;
        mock_.satisfaction.expire(mockHours_);
        return true;
    }

    [[nodiscard]] const ProgressionState& current() const
    {
        return developer_ ? mock_ : live_;
    }

    // No QA state is returned to a save adapter.
    [[nodiscard]] std::optional<ProgressionState> stateForSave() const
    {
        if (developer_) return std::nullopt;
        return live_;
    }

    // The adapter supplies validated data belonging to the newly loaded save.
    bool loadState(ProgressionState state)
    {
        if (state.stats && !valid(*state.stats)) return false;
        developer_=false;
        mock_={};
        mockHours_=0;
        mockCalendarHours_.reset();
        live_ = std::move(state);
        settled_.reset();
        ++saveGeneration_;
        return true;
    }

    void setNodesEnabled(bool enabled) { editable().nodes.enabled = enabled; }

    bool buy(Node node)
    {
        auto& state = editable();
        return state.stats && purchase(state.nodes,node,(*state.stats)[N]);
    }

    std::int64_t offerSatisfaction() {
        auto& state=editable();
        if (!state.stats || !state.nodes.active(Node::Recovery1)) return -1;
        const auto positive=appliedSatisfaction(state.satisfaction.total());
        if (positive<=0) return -1;
        const auto gain=positive/2; // Existing X/K ledger uses integer units.
        if (gain>std::numeric_limits<std::int64_t>::max()-state.corruption) return -1;
        state.satisfaction.consumePositiveNet();
        state.corruption+=gain;
        return gain;
    }

    bool resetAll()
    {
        auto& state = editable();
        if (!state.stats) return false;
        clearProgression(state);
        if (!developer_) settled_ = state.stats;
        return true;
    }

private:
    static void record(ProgressionState& state, Incident event)
    {
        std::shift_right(state.history.begin(),state.history.end(),1);
        state.history.front()=event;
    }

    static void resolveGxpFault(ProgressionState& state)
    {
        for (auto& event : state.history) {
            if (event == Incident::GxpFault) event = Incident::GxpRecovered;
        }
    }

    static void clearProgression(ProgressionState& state)
    {
        resolveGxpFault(state);
        state.nodes.purchased.fill(false);
        state.satisfaction = {};
        state.recentBlocks={};
        state.corruption = 0;
        state.soloProgress = 0;
    }

    void observe(ProgressionState& state, const Counters& sample)
    {
        bool decreased = false;
        if (state.lastValid) {
            for (std::size_t i=0; i<sample.size(); ++i) decreased |= sample[i]<(*state.lastValid)[i];
        }
        state.stats = state.lastValid = sample;
        if (decreased) {
            clearProgression(state);
            record(state,Incident::CounterReset);
        } else if (state.nodes.spent() > earnedGxp(sample[N]) &&
                   std::find(state.history.begin(),state.history.end(),Incident::GxpFault)==state.history.end()) {
            record(state,Incident::GxpFault);
        } else if (state.nodes.spent() <= earnedGxp(sample[N])) {
            resolveGxpFault(state);
        }
        if (&state == &live_ && (decreased || !settled_)) settled_=sample;
    }

public:

    [[nodiscard]] std::optional<int> totalGxp() const
    {
        const auto& sample = current().stats;
        if (!sample) return std::nullopt;
        return earnedGxp((*sample)[N]);
    }

    [[nodiscard]] std::optional<int> availableGxp() const
    {
        const auto total = totalGxp();
        if (!total || current().nodes.spent() > *total) return std::nullopt;
        return *total - current().nodes.spent();
    }

private:
    static bool valid(const Counters& sample)
    {
        return std::all_of(sample.begin(),sample.end(),[](int count){return count>=0;});
    }

    ProgressionState& editable() { return developer_ ? mock_ : live_; }

    ProgressionState live_;
    ProgressionState mock_;
    double mockHours_{};
    std::optional<double> mockCalendarHours_;
    std::optional<Counters> settled_;
    std::uint64_t saveGeneration_{};
    bool developer_{};
};

}  // namespace tds::progression