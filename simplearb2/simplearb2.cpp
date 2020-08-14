#include <iostream>
#include <string>
#include <algorithm>
#include <vector>
#include <deque>

#include "./simplearb2.h"

SimpleArb2::SimpleArb2(const libconfig::Setting & param_setting, std::unordered_map<std::string, std::vector<BaseStrategy*> >*ticker_strat_map, BaseSender<MarketSnapshot>* uisender, BaseSender<Order>* ordersender, TimeController* tc, ContractWorker* cw, HistoryWorker* hw, const std::string & date, StrategyMode::Enum mode, std::ofstream* exchange_file)
  : date_(date),
    max_close_try_(10),
    close_round_(0),
    sample_head_(0),
    sample_tail_(0),
    no_close_today_(false),
    exchange_file_(exchange_file) {
  m_tc = tc;
  m_cw = cw;
  m_hw = hw;
  // mids_.reserve(30000);
  SetStrategyMode(mode, exchange_file);
  if (FillStratConfig(param_setting)) {
    RunningSetup(ticker_strat_map, uisender, ordersender);
  }
}

SimpleArb2::~SimpleArb2() {
}

void SimpleArb2::RunningSetup(std::unordered_map<std::string, std::vector<BaseStrategy*> >*ticker_strat_map, BaseSender<MarketSnapshot>* uisender, BaseSender<Order>* ordersender) {
  m_ui_sender = uisender;
  m_order_sender = ordersender;
  (*ticker_strat_map)[main_ticker_].emplace_back(this);
  (*ticker_strat_map)[hedge_ticker_].emplace_back(this);
  (*ticker_strat_map)["positionend"].emplace_back(this);
  MarketSnapshot shot;
  m_shot_map[main_ticker_] = shot;
  m_shot_map[hedge_ticker_] = shot;
  m_avgcost_map[main_ticker_] = 0.0;
  m_avgcost_map[hedge_ticker_] = 0.0;
}

bool SimpleArb2::FillStratConfig(const libconfig::Setting& param_setting) {
  try {
    std::string unique_name = param_setting["unique_name"];
    const libconfig::Setting & contract_setting = m_cw->Lookup(unique_name);
    m_strat_name = unique_name;
    auto v = m_hw->GetAllTicker(unique_name);
    if (v.size() < 2) {
      printf("no enough ticker for %s\n", unique_name.c_str());
      return false;
    }
    main_ticker_ = v[1].first;
    hedge_ticker_ = v[0].first;
    max_pos_ = param_setting["max_position"];
    train_samples_ = param_setting["train_samples"];
    double m_r = param_setting["min_range"];
    double m_p = param_setting["min_profit"];
    min_price_move_ = contract_setting["min_price_move"];
    min_profit_ = m_p * min_price_move_;
    min_range_ = m_r * min_price_move_;
    double spread_threshold_int = param_setting["spread_threshold"];
    spread_threshold_ = spread_threshold_int*min_price_move_;
    m_max_holding_sec = param_setting["max_holding_sec"];
    range_width_ = param_setting["range_width"];
    std::string con = GetCon(main_ticker_);
    cancel_limit_ = contract_setting["cancel_limit"];
    max_round_ = param_setting["max_round"];
    if (param_setting.exists("no_close_today")) {
      no_close_today_ = param_setting["no_close_today"];
    }
  } catch(const libconfig::SettingNotFoundException &nfex) {
    printf("Setting '%s' is missing", nfex.getPath());
    exit(1);
  } catch(const libconfig::SettingTypeException &tex) {
    printf("Setting '%s' has the wrong type", tex.getPath());
    exit(1);
  } catch (const std::exception& ex) {
    printf("EXCEPTION: %s\n", ex.what());
    exit(1);
  }
  return true;
}

void SimpleArb2::Stop() {
  CancelAll(main_ticker_);
  m_ss = StrategyStatus::Stopped;
}

void SimpleArb2::DoOperationAfterCancelled(Order* o) {
  printf("ticker %s cancel num %d!\n", o->ticker, m_cancel_map[o->ticker]);
  if (m_cancel_map[o->ticker] > cancel_limit_) {
    printf("ticker %s hit cancel limit!\n", o->ticker);
    Stop();
  }
}

double SimpleArb2::OrderPrice(const std::string & ticker, OrderSide::Enum side, bool control_price) {
  if (m_mode == StrategyMode::NextTest) {
    if (ticker == hedge_ticker_) {
      return (side == OrderSide::Buy)?m_next_shot_map[ticker].asks[0]:m_next_shot_map[ticker].bids[0];
    } else if (ticker == main_ticker_) {
      return (side == OrderSide::Buy)?m_shot_map[ticker].asks[0]:m_shot_map[ticker].bids[0];
    } else {
      printf("error ticker %s\n", ticker.c_str());
      return -1.0;
    }
  } else {
    if (ticker == hedge_ticker_) {
      return (side == OrderSide::Buy) ? m_shot_map[hedge_ticker_].asks[0] : m_shot_map[hedge_ticker_].bids[0];
    } else if (ticker == main_ticker_) {
      // price hunter mode
      return (side == OrderSide::Buy) ? RoundPrice(m_shot_map[hedge_ticker_].bids[0] + down_diff_, min_price_move_, 1) : RoundPrice(m_shot_map[hedge_ticker_].asks[0] + up_diff_, min_price_move_, -1);
    } else {
      printf("error ticker %s\n", ticker.c_str());
      return -1.0;
    }
  }
}

void SimpleArb2::ForceFlat() {
  int pos = m_position_map[main_ticker_];
  if (pos == 0) {
    return;
  }
  OrderSide::Enum side = (pos > 0) ? OrderSide::Sell : OrderSide::Buy;
  for (int i = 0; i < max_close_try_; i++) {
    if (Close(side)) {
      break;
    }
    if (i == max_close_try_ - 1) {
      printf("[%s %s]try max_close times, cant close this order!\n", main_ticker_.c_str(), hedge_ticker_.c_str());
      PrintMap(m_order_map);
      m_order_map.clear();  // it's a temp solution, TODO
      Close(side);
    }
  }
}

bool SimpleArb2::Close(OrderSide::Enum side) {
  if (!m_order_map.empty()) {
    printf("[%s %s]block order exsited! no close\n", main_ticker_.c_str(), hedge_ticker_.c_str());
    PrintMap(m_order_map);
    return false;
  }
  double price = (side == OrderSide::Buy) ? m_shot_map[main_ticker_].asks[0] : m_shot_map[main_ticker_].bids[0];
  int64_t size = (side == OrderSide::Buy) ? 1 : -1;
  target_hedge_price_ = (side == OrderSide::Buy) ? m_shot_map[hedge_ticker_].bids[0] : m_shot_map[hedge_ticker_].asks[0];
  Order* o = PlaceOrder(main_ticker_, price, size, no_close_today_, "close");
  o->Show(stdout);
  return true;
}

void SimpleArb2::CloseLogic() {
  int pos = m_position_map[main_ticker_];
  if (pos == 0) {
    return;
  }
  double mid = mids_.back();
  if (pos > 0 && mid > mean_ + current_spread_/2) {
    printf("[%s %s]CloseLogic: mid=%lf, mean=%lf, pos=%d, current_spread_=%lf\n", main_ticker_.c_str(), hedge_ticker_.c_str(), mid, mean_, pos, current_spread_);
    m_shot_map[main_ticker_].Show(stdout);
    m_shot_map[hedge_ticker_].Show(stdout);
    Close(OrderSide::Sell);
  } else if (pos < 0 && mid < mean_ - current_spread_/2) {
    printf("[%s %s]CloseLogic: mid=%lf, mean=%lf, pos=%d\n", main_ticker_.c_str(), hedge_ticker_.c_str(), mid, mean_, pos);
    m_shot_map[main_ticker_].Show(stdout);
    m_shot_map[hedge_ticker_].Show(stdout);
    Close(OrderSide::Buy);
  }
}

void SimpleArb2::SoftCloseLogic() {
  int pos = m_position_map[main_ticker_];
  if (pos == 0) {
    return;
  }
  double mid = mids_.back();
  double softmean = (std::get<0>(CalMeanStd(mids_, sample_tail_-100, 100)) + mean_) / 2;
  // double softmean = (std::get<0>(CalMeanStd(mids_, sample_tail_-100, 100)));
  if (pos > 0 && mid > softmean + current_spread_/2) {
    printf("[%s %s]CloseLogic: mid=%lf, softmean=%lf, pos=%d, current_spread_=%lf\n", main_ticker_.c_str(), hedge_ticker_.c_str(), mid, softmean, pos, current_spread_);
    m_shot_map[main_ticker_].Show(stdout);
    m_shot_map[hedge_ticker_].Show(stdout);
    Close(OrderSide::Sell);
  } else if (pos < 0 && mid < softmean - current_spread_/2) {
    printf("[%s %s]CloseLogic: mid=%lf, softmean=%lf, pos=%d\n", main_ticker_.c_str(), hedge_ticker_.c_str(), mid, softmean, pos);
    m_shot_map[main_ticker_].Show(stdout);
    m_shot_map[hedge_ticker_].Show(stdout);
    Close(OrderSide::Buy);
  }
}

void SimpleArb2::Flatting() {
  if (IsAlign()) {
    CloseLogic();
  }
}

void SimpleArb2::Open(OrderSide::Enum side) {
  if (!m_order_map.empty()) {
    printf("block order exsited! no open \n");
    PrintMap(m_order_map);
    return;
  }
  double price = (side == OrderSide::Buy) ? m_shot_map[main_ticker_].asks[0] : m_shot_map[main_ticker_].bids[0];
  int64_t size = (side == OrderSide::Buy) ? 1 : -1;
  Order* o = PlaceOrder(main_ticker_, price, size, no_close_today_, "open");
  target_hedge_price_ = (side == OrderSide::Buy) ? m_shot_map[hedge_ticker_].bids[0] : m_shot_map[hedge_ticker_].asks[0];
  o->Show(stdout);
}

bool SimpleArb2::OpenLogic() {
  if (abs(m_position_map[main_ticker_]) >= max_pos_ || !m_order_map.empty()) {
    return false;
  }
  auto main_shot = m_shot_map[main_ticker_];
  auto hedge_shot = m_shot_map[hedge_ticker_];
  if (main_shot.asks[0] - hedge_shot.asks[0] >= up_diff_) {  // sell at high price
    if (hedge_shot.ask_sizes[0] < 5) {  // filter those too thin oppounity
      return false;
    }
    PlaceOrder(main_ticker_, main_shot.asks[0], -1, no_close_today_, "open")->Show(stdout);
  } else if (main_shot.bids[0] - hedge_shot.bids[0] <= down_diff_) {  // buy at low price
    if (hedge_shot.bid_sizes[0] < 5) {  // filter those too thin oppounity
      return false;
    }
    PlaceOrder(main_ticker_, main_shot.bids[0], 1, no_close_today_, "open")->Show(stdout);
  } else {
    return false;
  }
  m_shot_map[main_ticker_].Show(stdout);
  m_shot_map[hedge_ticker_].Show(stdout);
  return true;
}

void SimpleArb2::Run() {
  if (!IsAlign() || close_round_ >= max_round_) {
    return;
  }
  if (OpenLogic()) {
    return;
  }
  CloseLogic();
}

void SimpleArb2::UpdateParams(const std::string& tag) {
  if (sample_tail_ < train_samples_) {
    printf("calparams wrong, exit\n");
    exit(1);
  }
  printf("[%s %s] sample_tail_=%d, sample_head_=%d, train_samples_=%d\n", main_ticker_.c_str(), hedge_ticker_.c_str(), sample_tail_, sample_head_, train_samples_);
  auto r = CalMeanStd(mids_, sample_tail_ - train_samples_, train_samples_);
  double avg = std::get<0>(r);
  double std = std::get<1>(r);
  FeePoint main_point = m_cw->CalFeePoint(main_ticker_, GetMid(main_ticker_), 1, GetMid(main_ticker_), 1, no_close_today_);
  FeePoint hedge_point = m_cw->CalFeePoint(hedge_ticker_, GetMid(hedge_ticker_), 1, GetMid(hedge_ticker_), 1, no_close_today_);
  double round_fee_cost = main_point.open_fee_point + main_point.close_fee_point + hedge_point.open_fee_point + hedge_point.close_fee_point;
  double margin = std::max(range_width_ * std, min_range_) + round_fee_cost;
  up_diff_ = avg + margin;
  down_diff_ = avg - margin;
  mean_ = avg;
  spread_threshold_ = margin - min_profit_ - round_fee_cost;
  printf("[%s %s]%s cal done,mean is %lf, std is %lf, parmeters: [%lf,%lf], spread_threshold is %lf, min_profit is %lf, fee_point=%lf\n", main_ticker_.c_str(), hedge_ticker_.c_str(), tag.c_str(), avg, std, down_diff_, up_diff_, spread_threshold_, min_profit_, round_fee_cost);
  sample_head_ = sample_tail_;
}

void SimpleArb2::DoOperationAfterUpdateData(const MarketSnapshot& shot) {
  current_spread_ = m_shot_map[main_ticker_].asks[0] - m_shot_map[main_ticker_].bids[0];
  if (IsAlign()) {  // && Spread_Good()) {
    mids_.push_back(GetMid(main_ticker_) - GetMid(hedge_ticker_));
    printf("[%s %s]mid_diff=%lf, head:%d, tail:%d\n", main_ticker_.c_str(), hedge_ticker_.c_str(), mids_.back(), sample_head_, sample_tail_);
    if (++ sample_tail_ - sample_head_ > train_samples_) {
      UpdateParams("[tail-head hit]");
    }
  }
}

void SimpleArb2::Resume() {
  sample_head_ = sample_tail_;
}

bool SimpleArb2::Ready() {
  return sample_tail_ - sample_head_ >= train_samples_;
}

void SimpleArb2::ModerateOrders(const std::string & ticker) {
  if (m_mode != StrategyMode::Real) {
    return;
  }
  for (auto m : m_order_map) {
    Order* o = m.second;
    if (!o->Valid()) {
      continue;
    }
    // MarketSnapshot shot = m_shot_map[o->ticker];
    double reasonable_price = OrderPrice(o->ticker, o->side, false);
    if (fabs(reasonable_price - o->price) < min_price_move_ / 2) {  // this tick is the order sent tick or tick price not changed
      return;
    }
    if (o->ticker == main_ticker_) {
      if ((o->side == OrderSide::Buy && o->price - reasonable_price >= min_price_move_ / 2)  //  buy, order price > reasonable buy price, loss
       || (o->side == OrderSide::Sell && o->price - reasonable_price <= min_price_move_ / 2)) {  // sell, order price < reasonable sell
        CancelOrder(o);
      }
    } else if (o->ticker == hedge_ticker_) {
      ModOrder(o);
    } else {
      continue;
    }
  }
}

void SimpleArb2::Start() {
  UpdateParams("[start]");
}

void SimpleArb2::DoOperationAfterFilled(Order* o, const ExchangeInfo& info) {
    std::string tbd = o->tbd;
  bool is_close = (tbd.find("close") != string::npos);
  o->Show(stdout);
  if (strcmp(info.ticker, main_ticker_.c_str()) == 0) {
    double price = (info.side == OrderSide::Buy) ? m_shot_map[hedge_ticker_].bids[0] : m_shot_map[hedge_ticker_].asks[0];
    if (m_mode == StrategyMode::NextTest) {
      price = (info.side == OrderSide::Buy) ? m_next_shot_map[hedge_ticker_].bids[0] : m_next_shot_map[hedge_ticker_].asks[0];
    }
    int64_t size = (info.side == OrderSide::Buy) ? -1 : 1;
    string orderinfo = is_close ? "close" : "open";
    Order* o = PlaceOrder(hedge_ticker_, price, size, no_close_today_, orderinfo);
    o->Show(stdout);
  } else if (strcmp(info.ticker, hedge_ticker_.c_str()) == 0) {
    if (is_close) {
      close_round_++;
      UpdateParams("[close]");
    } else {
      sample_head_ = sample_tail_;
    }
  } else {
  }
}

bool SimpleArb2::Spread_Good() {
  return current_spread_ <= spread_threshold_;
}

bool SimpleArb2::IsAlign() {
  if (m_shot_map[main_ticker_].time.tv_sec == m_shot_map[hedge_ticker_].time.tv_sec && abs(m_shot_map[main_ticker_].time.tv_usec-m_shot_map[hedge_ticker_].time.tv_usec) < 100000) {
    return true;
  }
  return false;
}
