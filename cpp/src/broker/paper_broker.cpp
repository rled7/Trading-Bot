#include <memory>
#include <algorithm>
/**
 * AlgoForge — src/broker/paper_broker.cpp
 * Paper trading broker. Simulates order fills, SL/TP, slippage.
 * Generates synthetic OHLCV price data from a random walk.
 */
#include "broker/broker.hpp"
#include "core/config.hpp"
#include <unordered_map>
#include <vector>
#include <mutex>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cassert>
#include <random>
#include <algorithm>

namespace af {

/* ── Default spreads ── */
static double default_spread(const char *sym) {
    if (!sym) return 0.0001;
    std::string s(sym);
    if (s=="XAUUSD"||s=="XAGUSD") return 0.25;
    if (s=="USDJPY"||s=="EURJPY") return 0.030;
    if (s.find("JPY")!=std::string::npos) return 0.030;
    return 0.00012;
}

/* ── Price simulator ── */
struct PriceSim {
    double mid = 1.0;
    double volatility = 0.008;
    std::mt19937 rng{42};

    PriceSim() = default;
    PriceSim(double start, double vol, uint32_t seed=42)
        : mid(start), volatility(vol), rng(seed) {}

    double next_mid() {
        std::normal_distribution<double> nd(0.0, volatility / std::sqrt(24.0*60.0));
        mid = std::max(mid * (1.0 + nd(rng)), 0.0001);
        return mid;
    }
};

/* ── PaperBroker ── */
class PaperBroker final : public IBroker {
public:
    explicit PaperBroker(double initial_balance = 10000.0)
        : balance_(initial_balance), equity_(initial_balance)
    {
        /* Seed price simulators */
        price_sim_["EURUSD"] = PriceSim(1.08500, 0.0080, 1);
        price_sim_["GBPUSD"] = PriceSim(1.26500, 0.0100, 2);
        price_sim_["USDJPY"] = PriceSim(149.500, 0.0070, 3);
        price_sim_["XAUUSD"] = PriceSim(2050.00, 0.0120, 4);
        price_sim_["EURJPY"] = PriceSim(162.000, 0.0090, 5);
        price_sim_["GBPJPY"] = PriceSim(188.000, 0.0110, 6);
        price_sim_["AUDUSD"] = PriceSim(0.65500, 0.0090, 7);
        price_sim_["USDCAD"] = PriceSim(1.36000, 0.0075, 8);
        price_sim_["USDCHF"] = PriceSim(0.90500, 0.0070, 9);
        price_sim_["EURGBP"] = PriceSim(0.85800, 0.0060,10);
    }

    AF_Error connect() override {
        connected_ = true;
        snprintf(acct_.currency, sizeof(acct_.currency), "USD");
        acct_.balance     = balance_;
        acct_.equity      = equity_;
        acct_.free_margin = balance_;
        acct_.margin      = 0;
        acct_.leverage    = 100;
        acct_.login       = 99999999;
        printf("[PAPER] Connected | balance=%.2f\n", balance_);
        return AF_OK;
    }

    void disconnect() override { connected_ = false; }
    bool is_connected() const override { return connected_; }
    const char* broker_name() const override { return "PaperSimulator"; }

    AF_Error get_account(AF_AccountInfo &out) const override {
        std::lock_guard lk(mu_);
        update_equity();
        out         = acct_;
        out.balance = balance_;
        out.equity  = equity_;
        return AF_OK;
    }

    AF_Error get_tick(const char *symbol, AF_Tick &out) const override {
        if (!symbol) return AF_ERR_INVALID_SYMBOL;
        std::lock_guard lk(mu_);
        auto it = price_sim_.find(symbol);
        if (it == price_sim_.end()) return AF_ERR_INVALID_SYMBOL;
        double mid = const_cast<PriceSim&>(it->second).next_mid();
        double sp  = default_spread(symbol);
        out.bid    = mid - sp/2.0;
        out.ask    = mid + sp/2.0;
        out.volume = 500.0 + std::uniform_real_distribution<>(0,2000)(
                     const_cast<std::mt19937&>(rng_global_));
        out.timestamp = 0; /* would be time(NULL) in production */
        snprintf(out.symbol, sizeof(out.symbol), "%s", symbol);
        last_ticks_[symbol] = out;
        return AF_OK;
    }

    AF_Error get_symbol_info(const char *symbol, AF_SymbolInfo &out) const override {
        if (!symbol) return AF_ERR_INVALID_SYMBOL;
        snprintf(out.name, sizeof(out.name), "%s", symbol);
        std::string s(symbol);
        out.digits        = (s=="XAUUSD"||s=="XAGUSD") ? 2 : (s.find("JPY")!=std::string::npos?3:5);
        out.point         = std::pow(10.0, -out.digits);
        out.contract_size = 100000.0;
        out.volume_min    = 0.01;
        out.volume_max    = 100.0;
        out.volume_step   = 0.01;
        return AF_OK;
    }

    AF_Error get_bars(const char *symbol, AF_Timeframe tf,
                       int count, AF_Bar *bars_out, int *filled) const override {
        if (!symbol || !bars_out || !filled || count<=0) return AF_ERR_INVALID_PARAM;
        std::lock_guard lk(mu_);
        auto it = price_sim_.find(symbol);
        if (it == price_sim_.end()) return AF_ERR_INVALID_SYMBOL;

        /* Generate synthetic bars via random walk */
        PriceSim sim = it->second;
        double vol_per_bar = 0.0080 / std::sqrt(86400.0 / tf) / std::sqrt(252.0);
        std::mt19937 rng(42 + static_cast<uint32_t>(tf));
        std::normal_distribution<double> nd(0.0, vol_per_bar);

        double price = sim.mid;
        for (int i = 0; i < count; i++) {
            double o = price;
            double h = o, l = o;
            for (int t=0;t<4;t++){
                double p=o*(1+nd(rng));
                if (p>h)h=p; if (p<l)l=p;
            }
            double c = o * (1+nd(rng));
            if (c>h)h=c; if (c<l)l=c;
            bars_out[i].timestamp = 0;
            bars_out[i].open = o; bars_out[i].high = h;
            bars_out[i].low  = l; bars_out[i].close= c;
            bars_out[i].volume= 500+std::uniform_real_distribution<>(0,4500)(rng);
            bars_out[i].spread= default_spread(symbol);
            af_bar_init(&bars_out[i]);
            price = c;
        }
        *filled = count;
        return AF_OK;
    }

    AF_Error place_order(const char *symbol, AF_OrderType type,
                          double lots, double price,
                          double sl, double tp,
                          uint32_t magic, const char *comment,
                          AF_Order &out) override {
        std::lock_guard lk(mu_);
        if (!symbol) return AF_ERR_INVALID_SYMBOL;
        if (lots < 0.01) return AF_ERR_INVALID_LOTS;

        /* Resolve market price */
        AF_Tick tick{};
        auto it = price_sim_.find(symbol);
        if (it == price_sim_.end()) return AF_ERR_INVALID_SYMBOL;
        double mid = const_cast<PriceSim&>(it->second).next_mid();
        double sp  = default_spread(symbol);
        tick.bid = mid-sp/2; tick.ask = mid+sp/2;

        bool is_market = (type==AF_ORDER_BUY||type==AF_ORDER_SELL);
        double fill_p  = 0;
        if (type==AF_ORDER_BUY  || type==AF_ORDER_BUY_STOP)   fill_p = tick.ask;
        if (type==AF_ORDER_SELL || type==AF_ORDER_SELL_STOP)   fill_p = tick.bid;
        if (type==AF_ORDER_BUY_LIMIT)  fill_p = std::min(price, tick.ask);
        if (type==AF_ORDER_SELL_LIMIT) fill_p = std::max(price, tick.bid);

        double commission = 7.0 * lots;

        AF_Position pos{};
        pos.ticket      = next_ticket_++;
        snprintf(pos.symbol, sizeof(pos.symbol), "%s", symbol);
        pos.side        = (type==AF_ORDER_BUY||type==AF_ORDER_BUY_LIMIT||type==AF_ORDER_BUY_STOP)
                          ? AF_DIR_LONG : AF_DIR_SHORT;
        pos.lots        = lots;
        pos.open_price  = fill_p;
        pos.current_price= fill_p;
        pos.sl          = sl;
        pos.tp          = tp;
        pos.profit      = 0;
        pos.commission  = -commission;
        pos.magic       = magic;
        if (comment) snprintf(pos.comment, sizeof(pos.comment), "%s", comment);

        positions_[pos.ticket] = pos;
        balance_ -= commission;

        /* Fill result */
        out.ticket     = pos.ticket;
        out.fill_price = fill_p;
        snprintf(out.symbol, sizeof(out.symbol), "%s", symbol);
        out.type       = type;
        out.lots       = lots;
        out.sl         = sl;
        out.tp         = tp;
        out.magic      = magic;

        printf("[PAPER] %s %.2f %s @ %.5f | SL=%.5f TP=%.5f | comm=-%.2f\n",
               pos.side==AF_DIR_LONG?"BUY":"SELL", lots, symbol, fill_p, sl, tp, commission);

        return AF_OK;
    }

    AF_Error close_position(uint64_t ticket, double lots_to_close = 0.0) override {
        std::lock_guard lk(mu_);
        auto it = positions_.find(ticket);
        if (it == positions_.end()) return AF_ERR_POSITION_NOT_FOUND;
        AF_Position &pos = it->second;

        auto pit = price_sim_.find(pos.symbol);
        double mid = (pit!=price_sim_.end())
                     ? const_cast<PriceSim&>(pit->second).next_mid() : pos.open_price;
        double sp  = default_spread(pos.symbol);
        double exit_price = (pos.side==AF_DIR_LONG) ? mid-sp/2 : mid+sp/2;

        double close_lots = (lots_to_close>0 && lots_to_close < pos.lots)
                            ? lots_to_close : pos.lots;
        double raw_pnl = pos.side==AF_DIR_LONG
                         ? (exit_price - pos.open_price) * close_lots * 100000.0
                         : (pos.open_price - exit_price) * close_lots * 100000.0;
        double comm = 0.0;  /* already charged on open */
        double net  = raw_pnl + comm;

        balance_ += net;
        trade_log_.push_back(pos);
        trade_log_.back().profit = net;
        trade_log_.back().current_price = exit_price;

        printf("[PAPER] CLOSE %s %s %.2f @ %.5f | P&L=%.2f\n",
               pos.symbol, pos.side==AF_DIR_LONG?"LONG":"SHORT",
               close_lots, exit_price, net);

        if (close_lots >= pos.lots) {
            positions_.erase(it);
        } else {
            pos.lots -= close_lots;
        }
        return AF_OK;
    }

    AF_Error modify_position(uint64_t ticket, double sl, double tp) override {
        std::lock_guard lk(mu_);
        auto it = positions_.find(ticket);
        if (it==positions_.end()) return AF_ERR_POSITION_NOT_FOUND;
        it->second.sl = sl;
        it->second.tp = tp;
        return AF_OK;
    }

    AF_Error cancel_order(uint64_t ticket) override {
        std::lock_guard lk(mu_);
        pending_.erase(ticket);
        return AF_OK;
    }

    std::vector<AF_Position> get_positions() const override {
        std::lock_guard lk(mu_);
        std::vector<AF_Position> v;
        for (auto &[t,p]:positions_) { update_position(const_cast<AF_Position&>(p)); v.push_back(p); }
        return v;
    }

    std::vector<AF_Order> get_orders() const override {
        std::lock_guard lk(mu_);
        std::vector<AF_Order> v;
        for (auto &[t,o]:pending_) v.push_back(o);
        return v;
    }

    std::optional<AF_Position> get_position(uint64_t ticket) const override {
        std::lock_guard lk(mu_);
        auto it = positions_.find(ticket);
        if (it==positions_.end()) return std::nullopt;
        return it->second;
    }

    void poll_sl_tp() override {
        std::lock_guard lk(mu_);
        std::vector<uint64_t> to_close;
        for (auto &[ticket, pos] : positions_) {
            auto pit = price_sim_.find(pos.symbol);
            if (pit==price_sim_.end()) continue;
            double mid= const_cast<PriceSim&>(pit->second).next_mid();
            double sp = default_spread(pos.symbol);
            double bid= mid-sp/2, ask= mid+sp/2;
            pos.current_price = (pos.side==AF_DIR_LONG) ? bid : ask;
            if (pos.sl > 0) {
                bool sl_hit = (pos.side==AF_DIR_LONG && bid<=pos.sl) ||
                              (pos.side==AF_DIR_SHORT && ask>=pos.sl);
                if (sl_hit) { to_close.push_back(ticket); continue; }
            }
            if (pos.tp > 0) {
                bool tp_hit = (pos.side==AF_DIR_LONG && bid>=pos.tp) ||
                              (pos.side==AF_DIR_SHORT && ask<=pos.tp);
                if (tp_hit) to_close.push_back(ticket);
            }
        }
        /* Close outside lock — recursive close_position would deadlock */
        mu_.unlock();
        for (uint64_t t : to_close) close_position(t);
        mu_.lock();
    }

    const std::vector<AF_Position>& trade_log() const { return trade_log_; }

private:
    mutable std::mutex mu_;
    bool connected_{false};
    double balance_; mutable double equity_;
    mutable AF_AccountInfo acct_{};

    mutable std::unordered_map<std::string, PriceSim> price_sim_;
    mutable std::unordered_map<std::string, AF_Tick>  last_ticks_;
    std::unordered_map<uint64_t, AF_Position>         positions_;
    std::unordered_map<uint64_t, AF_Order>            pending_;
    std::vector<AF_Position>                          trade_log_;
    uint64_t next_ticket_{1000001};
    mutable std::mt19937 rng_global_{1337};

    void update_equity() const {
        double floating = 0;
        for (auto &[t,p]:positions_) {
            update_position(const_cast<AF_Position&>(p));
            floating += p.profit;
        }
        equity_ = balance_ + floating;
        acct_.equity = equity_;
        acct_.profit = floating;
    }

    void update_position(AF_Position &p) const {
        auto it = price_sim_.find(p.symbol);
        if (it==price_sim_.end()) return;
        double mid = const_cast<PriceSim&>(it->second).next_mid();
        double sp  = default_spread(p.symbol);
        p.current_price= (p.side==AF_DIR_LONG) ? mid-sp/2 : mid+sp/2;
        double raw = (p.side==AF_DIR_LONG)
                     ? (p.current_price-p.open_price)*p.lots*100000.0
                     : (p.open_price-p.current_price)*p.lots*100000.0;
        p.profit = raw + p.commission;
    }
};

/* ── Factory ── */
std::unique_ptr<IBroker> make_paper_broker(double balance) {
    return std::make_unique<PaperBroker>(balance);
}

} /* namespace af */
