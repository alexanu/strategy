#include <iostream>
#include <string>
#include <algorithm>
#include <vector>
#include <deque>

#include "./simplearb.h"
void PrintDeque(const std::deque<double> & d) {
  std::cout << "start print deque: ";
  for (auto i : d) {
    std::cout << i << " ";
  }
  std::cout << "end print" << std::endl;
}

SimpleArb::SimpleArb(const libconfig::Setting & param_setting, std::unordered_map<std::string, std::vector<BaseStrategy*> >*ticker_strat_map, BaseSender<MarketSnapshot>* uisender, BaseSender<Order>* ordersender, TimeController* tc, ContractWorker* cw, HistoryWorker* hw, const std::string & date, StrategyMode::Enum mode, std::ofstream* exchange_file)
  : date(date),
    last_valid_mid(0.0),
    stop_loss_times(0),
    max_close_try(10),
    no_close_today(false),
    max_round(10000),
    close_round(0),
    sample_head(0),
    sample_tail(0),
    exchange_file(exchange_file) {
  m_tc = tc;
  m_cw = cw;
  m_hw = hw;
  SetStrategyMode(mode, exchange_file);
  if (FillStratConfig(param_setting)) {
    RunningSetup(ticker_strat_map, uisender, ordersender);
  }
}

SimpleArb::~SimpleArb() {
}

void SimpleArb::RunningSetup(std::unordered_map<std::string, std::vector<BaseStrategy*> >*ticker_strat_map, BaseSender<MarketSnapshot>* uisender, BaseSender<Order>* ordersender) {
  m_ui_sender = uisender;
  m_order_sender = ordersender;
  (*ticker_strat_map)[main_ticker].emplace_back(this);
  (*ticker_strat_map)[hedge_ticker].emplace_back(this);
  (*ticker_strat_map)["positionend"].emplace_back(this);
  MarketSnapshot shot;
  m_shot_map[main_ticker] = shot;
  m_shot_map[hedge_ticker] = shot;
  m_avgcost_map[main_ticker] = 0.0;
  m_avgcost_map[hedge_ticker] = 0.0;
}

bool SimpleArb::FillStratConfig(const libconfig::Setting& param_setting) {
  try {
    std::string unique_name = param_setting["unique_name"];
    const libconfig::Setting & contract_setting = m_cw->Lookup(unique_name);
    m_strat_name = unique_name;
    auto v = m_hw->GetAllTicker(unique_name);
    if (v.size() < 2) {
      printf("no enough ticker for %s\n", unique_name.c_str());
      // PrintVector(v);
      return false;
    }
    main_ticker = v[1].first;
    hedge_ticker = v[0].first;
    max_pos = param_setting["max_position"];
    train_samples = param_setting["train_samples"];
    double m_r = param_setting["min_range"];
    double m_p = param_setting["min_profit"];
    min_price_move = contract_setting["min_price_move"];
    min_profit = m_p * min_price_move;
    min_range = m_r * min_price_move;
    double add_margin = param_setting["add_margin"];
    increment = add_margin*min_price_move;
    double spread_threshold_int = param_setting["spread_threshold"];
    spread_threshold = spread_threshold_int*min_price_move;
    stop_loss_margin = param_setting["stop_loss_margin"];
    max_loss_times = param_setting["max_loss_times"];
    m_max_holding_sec = param_setting["max_holding_sec"];
    range_width = param_setting["range_width"];
    std::string con = GetCon(main_ticker);
    cancel_limit = contract_setting["cancel_limit"];
    max_round = param_setting["max_round"];
    split_num = param_setting["split_num"];
    if (param_setting.exists("no_close_today")) {
      no_close_today = param_setting["no_close_today"];
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
  up_diff = 0.0;
  down_diff = 0.0;
  stop_loss_up_line = 0.0;
  stop_loss_down_line = 0.0;
  return true;
}

void SimpleArb::Stop() {
  CancelAll(main_ticker);
  m_ss = StrategyStatus::Stopped;
}

inline bool SimpleArb::IsAlign() {
  if (m_shot_map[main_ticker].time.tv_sec == m_shot_map[hedge_ticker].time.tv_sec && abs(m_shot_map[main_ticker].time.tv_usec-m_shot_map[hedge_ticker].time.tv_usec) < 100000) {
    return true;
  }
  return false;
}

OrderSide::Enum SimpleArb::OpenLogicSide() {
  double mid = GetPairMid();
  // printf("judge open logic side:mid = %lf, up_diff=%lf, down_diff=%lf\n", mid, up_diff, down_diff);
  // m_shot_map[main_ticker].Show(stdout);
  // m_shot_map[hedge_ticker].Show(stdout);
  if (mid - current_spread/2 > up_diff) {
    printf("[%s %s]sell condition hit, as diff id %f\n",  main_ticker.c_str(), hedge_ticker.c_str(), mid);
    return OrderSide::Sell;
  } else if (mid + current_spread/2 < down_diff) {
    printf("[%s %s]buy condition hit, as diff id %f\n", main_ticker.c_str(), hedge_ticker.c_str(), mid);
    return OrderSide::Buy;
  } else {
    return OrderSide::Unknown;
  }
}

void SimpleArb::DoOperationAfterCancelled(Order* o) {
  printf("ticker %s cancel num %d!\n", o->ticker, m_cancel_map[o->ticker]);
  if (m_cancel_map[o->ticker] > cancel_limit) {
    printf("ticker %s hit cancel limit!\n", o->ticker);
    Stop();
  }
}

double SimpleArb::OrderPrice(const std::string & ticker, OrderSide::Enum side, bool control_price) {
  if (m_mode == StrategyMode::NextTest) {
    // double slip = (side == OrderSide::Buy)? m_shot_map[ticker].asks[0] - m_next_shot_map[ticker].asks[0] : m_next_shot_map[ticker].bids[0] - m_shot_map[ticker].bids[0];
    if (ticker == hedge_ticker) {
      // printf("Slip hedge[%s] %s: %lf %lf ->> %lf %lf pnl:%lf\n", ticker.c_str(), OrderSide::ToString(side), m_shot_map[ticker].asks[0], m_shot_map[ticker].bids[0], m_next_shot_map[ticker].asks[0], m_next_shot_map[ticker].bids[0], slip);
      return (side == OrderSide::Buy)?m_next_shot_map[ticker].asks[0]:m_next_shot_map[ticker].bids[0];
    } else if (ticker == main_ticker) {
      // printf("Slip main[%s] %s: %lf %lf ->> %lf %lf pnl:%lf\n", ticker.c_str(), OrderSide::ToString(side), m_shot_map[ticker].asks[0], m_shot_map[ticker].bids[0], m_next_shot_map[ticker].asks[0], m_next_shot_map[ticker].bids[0], slip);
      // return (side == OrderSide::Buy)?m_next_shot_map[ticker].asks[0]:m_next_shot_map[ticker].bids[0];
      return (side == OrderSide::Buy)?m_shot_map[ticker].asks[0]:m_shot_map[ticker].bids[0];
    } else {
      printf("error ticker %s\n", ticker.c_str());
      return -1.0;
    }
  } else {
    if (ticker == hedge_ticker) {
      return (side == OrderSide::Buy)?m_shot_map[hedge_ticker].asks[0]:m_shot_map[hedge_ticker].bids[0];
    } else if (ticker == main_ticker) {
      return (side == OrderSide::Buy)?m_shot_map[main_ticker].asks[0]:m_shot_map[main_ticker].bids[0];
    } else {
      printf("error ticker %s\n", ticker.c_str());
      return -1.0;
    }
  }
}

std::tuple<double, double> SimpleArb::CalMeanStd(const std::vector<double> & v, int head, int num) {
  std::vector<double> cal_v(v.begin() + head, v.begin() + head + num);
  double mean = 0.0;
  double std = 0.0;
  for (auto i : cal_v) {
    mean += i;
  }
  mean /= num;
  for (auto i : cal_v) {
    std += (i-mean) * (i-mean);
  }
  std /= num;
  std = sqrt(std);
  return std::tie(mean, std);
}

void SimpleArb::CalParams() {
  // int num_sample = sample_tail - sample_head;
  if (sample_tail < train_samples) {
    printf("[%s %s]no enough mid data! tail is %d\n", main_ticker.c_str(), hedge_ticker.c_str(), sample_tail);
    exit(1);
  }
  param_v.clear();
  auto r = CalMeanStd(map_vector, sample_tail - train_samples, train_samples);
  double avg = std::get<0>(r);
  double std = std::get<1>(r);
  /*
  unsigned int head = map_vector.size() - train_samples;
  for (int i = 0; i < split_num; ++i) {
    param_v.push_back(std::get<0>(CalMeanStd(map_vector, head+i*train_samples/split_num, train_samples/split_num)));
  }
  */
  FeePoint main_point = m_cw->CalFeePoint(main_ticker, GetMid(main_ticker), 1, GetMid(main_ticker), 1, no_close_today);
  FeePoint hedge_point = m_cw->CalFeePoint(hedge_ticker, GetMid(hedge_ticker), 1, GetMid(hedge_ticker), 1, no_close_today);
  double round_fee_cost = main_point.open_fee_point + main_point.close_fee_point + hedge_point.open_fee_point + hedge_point.close_fee_point;
  double margin = std::max(range_width * std, min_range) + round_fee_cost;
  up_diff = avg + margin;
  down_diff = avg - margin;
  stop_loss_up_line = up_diff + stop_loss_margin * margin;
  stop_loss_down_line = down_diff - stop_loss_margin * margin;
  // down_diff = std::min(avg - range_width * std, avg-min_profit);
  mean = avg;
  spread_threshold = margin - min_profit - round_fee_cost;
  printf("[%s %s]cal done,mean is %lf, std is %lf, parmeters: [%lf,%lf], spread_threshold is %lf, min_profit is %lf, up_loss=%lf, down_loss=%lf fee_point=%lf\n", main_ticker.c_str(), hedge_ticker.c_str(), avg, std, down_diff, up_diff, spread_threshold, min_profit, stop_loss_up_line, stop_loss_down_line, round_fee_cost);
  // char buffer[1024];
  // snprintf(buffer, sizeof(buffer), "CalParams %d->%d", sample_head, sample_tail);
  // tcr.EndTimer(buffer);
  sample_head = sample_tail;
}

bool SimpleArb::HitMean() {
  double this_mid = GetPairMid();
  int pos = m_position_map[main_ticker];
  if (pos > 0 && this_mid - current_spread/2 >= mean) {  // buy position
    printf("[%s %s] mean is %lf, this_mid is %lf, current_spread is %lf, pos is %d\n", main_ticker.c_str(), hedge_ticker.c_str(), mean, this_mid, current_spread, pos);
    return true;
  } else if (pos < 0 && this_mid + current_spread/2 <= mean) {  // sell position
    printf("[%s %s] mean is %lf, this_mid is %lf, current_spread is %lf, pos is %d\n", main_ticker.c_str(), hedge_ticker.c_str(), mean, this_mid, current_spread, pos);
    return true;
  }
  return false;
}

void SimpleArb::ForceFlat() {
  printf("%ld [%s %s]this round hit stop_loss condition, pos:%d current_mid:%lf, current_spread:%lf stoplossline %lf-%lf forceflat\n", m_shot_map[hedge_ticker].time.tv_sec, main_ticker.c_str(), hedge_ticker.c_str(), m_position_map[main_ticker], GetPairMid(), current_spread, stop_loss_down_line, stop_loss_up_line);
  m_shot_map[main_ticker].Show(stdout);
  m_shot_map[hedge_ticker].Show(stdout);
  for (int i = 0; i < max_close_try; i++) {
    if (Close(true)) {
      break;
    }
    if (i == max_close_try - 1) {
      printf("[%s %s]try max_close times, cant close this order!\n", main_ticker.c_str(), hedge_ticker.c_str());
      PrintMap(m_order_map);
      m_order_map.clear();  // it's a temp solution, TODO
      Close();
    }
  }
}

void SimpleArb::RecordSlip(const std::string & ticker, OrderSide::Enum side, bool is_close) {
    double slip = (side == OrderSide::Buy)? m_shot_map[ticker].asks[0] - m_next_shot_map[ticker].asks[0] : m_next_shot_map[ticker].bids[0] - m_shot_map[ticker].bids[0];
  if (ticker == hedge_ticker) {
    printf("Slip%s hedge[%s] %s: %lf %lf ->> %lf %lf pnl:%lf\n", is_close ? " close" : " open", ticker.c_str(), OrderSide::ToString(side), m_shot_map[ticker].asks[0], m_shot_map[ticker].bids[0], m_next_shot_map[ticker].asks[0], m_next_shot_map[ticker].bids[0], slip);
  } else if (ticker == main_ticker) {
    printf("Slip%s main[%s] %s: %lf %lf ->> %lf %lf pnl:%lf\n", is_close ? " close" : " open", ticker.c_str(), OrderSide::ToString(side), m_shot_map[ticker].asks[0], m_shot_map[ticker].bids[0], m_next_shot_map[ticker].asks[0], m_next_shot_map[ticker].bids[0], slip);
  } else {
    printf("error ticker %s\n", ticker.c_str());
  }
}

bool SimpleArb::Close(bool force_flat) {
  int pos = m_position_map[main_ticker];
  if (pos == 0) {
    return true;
  }
  // OrderSide::Enum pos_side = pos > 0 ? OrderSide::Buy: OrderSide::Sell;
  OrderSide::Enum close_side = pos > 0 ? OrderSide::Sell: OrderSide::Buy;
  if (NewHigh(close_side)) {
    printf("[%s %s]%s block orders bc new high appear!\n", main_ticker.c_str(), hedge_ticker.c_str(), OrderSide::ToString(close_side));
    PrintDeque(hedge_ask);
    PrintDeque(hedge_bid);
    return true;
  }
  // double hedge_price = pos > 0 ? m_shot_map[hedge_ticker].asks[0] : m_shot_map[hedge_ticker].bids[0];
  printf("close using %s: pos is %d, diff is %lf\n", OrderSide::ToString(close_side), pos, GetPairMid());
  PrintMap(m_position_map);
  // printf("spread is %lf %lf min_profit is %lf\n", m_shot_map[main_ticker].asks[0]-m_shot_map[main_ticker].bids[0], m_shot_map[hedge_ticker].asks[0]-m_shot_map[hedge_ticker].bids[0], min_profit);
  if (m_order_map.empty()) {
    PrintMap(m_avgcost_map);
    Order* o = NewOrder(main_ticker, close_side, abs(pos), false, false, force_flat ? "force_flat_close" : "close", no_close_today);  // close
    RecordSlip(main_ticker, o->side, true);
    // double slip = (o->side == OrderSide::Buy)? m_shot_map[main_ticker].asks[0] - m_next_shot_map[main_ticker].asks[0] : m_next_shot_map[main_ticker].bids[0] - m_shot_map[main_ticker].bids[0];
    // printf("Slip close main[%s] %s: %lf %lf ->> %lf %lf pnl:%lf\n", main_ticker.c_str(), OrderSide::ToString(o->side), m_shot_map[main_ticker].asks[0], m_shot_map[main_ticker].bids[0], m_next_shot_map[main_ticker].asks[0], m_next_shot_map[main_ticker].bids[0], slip);
    o->Show(stdout);
    HandleTestOrder(o);
    target_hedge_price = (close_side == OrderSide::Buy) ? m_shot_map[hedge_ticker].bids[0] : m_shot_map[hedge_ticker].asks[0];
    if (m_mode == StrategyMode::Real) {
      // RecordPnl(o);
      /*
      double this_round_pnl = m_cal.CalNetPnl(main_ticker, m_avgcost_map[main_ticker], abs(pos), o->price, abs(pos), close_side, no_close_today) + m_cal.CalNetPnl(hedge_ticker, m_avgcost_map[hedge_ticker], abs(pos), hedge_price, abs(pos), pos_side, no_close_today);
      Fee main_fee = m_cal.CalFee(main_ticker, m_avgcost_map[main_ticker], abs(pos), m_shot_map[main_ticker].  bids[0], abs(pos), no_close_today);
      Fee hedge_fee = m_cal.CalFee(hedge_ticker, m_avgcost_map[hedge_ticker], abs(pos), hedge_price, abs(pos), no_close_today);
      double this_round_fee = main_fee.open_fee + main_fee.close_fee + hedge_fee.open_fee + hedge_fee.close_fee;
      printf("%ld [%s %s]%sThis round close pnl: %lf, fee_cost: %lf pos is %d, holding second is %ld\n", m_shot_map[hedge_ticker].time.tv_sec, main_ticker.c_str(), hedge_ticker.c_str(), force_flat ? "[Time up] " : "", this_round_pnl, this_round_fee, pos, m_shot_map[hedge_ticker].time.tv_sec - build_position_time);
      */
    }
    return true;
  } else {
    printf("[%s %s]block order exsited! no close\n", main_ticker.c_str(), hedge_ticker.c_str());
    PrintMap(m_order_map);
    return false;
  }
}

double SimpleArb::GetPairMid() {
  return GetMid(main_ticker) - GetMid(hedge_ticker);
}

void SimpleArb::StopLossLogic() {
  if (!Spread_Good()) {
    return;
  }
  int pos = m_position_map[main_ticker];
  double this_mid = GetPairMid();
  if (pos > 0) {  // buy position
    if (this_mid < stop_loss_down_line) {  // stop condition meets
      ForceFlat();
      stop_loss_times += 1;
    }
  } else if (pos < 0) {  // sell position
    if (this_mid > stop_loss_up_line) {  // stop condition meets
      ForceFlat();
      stop_loss_times += 1;
    }
  }
  if (stop_loss_times >= max_loss_times) {
    m_ss = StrategyStatus::Stopped;
    printf("stop loss times hit max!\n");
  }
}

void SimpleArb::CloseLogic() {
  StopLossLogic();
  int pos = m_position_map[main_ticker];
  if (pos == 0) {
    return;
  }

  if (TimeUp()) {
    printf("[%s %s] holding time up, start from %ld, now is %ld, max_hold is %d close diff is %lf force to close position!\n", main_ticker.c_str(), hedge_ticker.c_str(), m_build_position_time, m_mode != StrategyMode::Real ? m_shot_map[main_ticker].time.tv_sec : m_tc->CurrentInt(), m_max_holding_sec, GetPairMid());
    ForceFlat();
    return;
  }

  if (HitMean()) {
    Close();
    return;
  }
}

void SimpleArb::Flatting() {
  if (IsAlign()) {
    CloseLogic();
  }
}

bool SimpleArb::NewHigh(OrderSide::Enum side) {
  return false;
  if (hedge_bid.size() < 6) {
    printf("no enough data in deque\n");
    return true;
  }
  if (side == OrderSide::Buy) {  // main side buy, hedgeside sell, should be bid
    auto temp = hedge_bid;
    temp.pop_back();
    double second_max_bid = 0.0;
    for (auto i : temp) {
      if (i > second_max_bid) {
        second_max_bid = i;
      }
    }
    return hedge_bid.back() - second_max_bid > 3*min_price_move ? true : false;
  } else if (side == OrderSide::Sell) {
    auto temp = hedge_ask;
    temp.pop_back();
    double second_min_ask = 10000000;
    for (auto i : temp) {
      if (i < second_min_ask) {
        second_min_ask = i;
      }
    }
    return second_min_ask - hedge_ask.back() > 3*min_price_move ? true : false;
  } else {
    return true;
  }
}


void SimpleArb::Open(OrderSide::Enum side) {
  if (NewHigh(side)) {
    printf("[%s %s]%s block orders bc new high appear!\n", main_ticker.c_str(), hedge_ticker.c_str(), OrderSide::ToString(side));
    PrintDeque(hedge_ask);
    PrintDeque(hedge_bid);
    return;
  }
  int pos = m_position_map[main_ticker];
  printf("[%s %s] open %s: pos is %d, diff is %lf\n", main_ticker.c_str(), hedge_ticker.c_str(), OrderSide::ToString(side), pos, GetPairMid());
  if (m_order_map.empty()) {  // no block order, can add open
    Order* o = NewOrder(main_ticker, side, 1, false, false, "open", no_close_today);
    RecordSlip(main_ticker, o->side);
    o->Show(stdout);
    // printf("spread is %lf %lf min_profit is %lf, next open will be %lf\n", m_shot_map[main_ticker].asks[0]-m_shot_map[main_ticker].bids[0], m_shot_map[hedge_ticker].asks[0]-m_shot_map[hedge_ticker].bids[0], min_profit, side == OrderSide::Buy ? down_diff: up_diff);
    HandleTestOrder(o);
    target_hedge_price = (side == OrderSide::Buy) ? m_shot_map[hedge_ticker].bids[0] : m_shot_map[hedge_ticker].asks[0];
    sample_head = sample_tail;
  } else {  // block order exsit, no open, possible reason: no enough margin
    printf("block order exsited! no open \n");
    PrintMap(m_order_map);
    // exit(1);
  }
}

bool SimpleArb::OpenLogic() {
  OrderSide::Enum side = OpenLogicSide();
  if (side == OrderSide::Unknown) {
    return false;
  }
  // do meet the logic
  int pos = m_position_map[main_ticker];
  if (abs(pos) == max_pos) {
    // hit max, still update bound
    // UpdateBound(side == OrderSide::Buy ? OrderSide::Sell : OrderSide::Buy);
    return false;
  }
  Open(side);
  return true;
}

void SimpleArb::Run() {
  if (IsAlign() && close_round < max_round) {
      if (!OpenLogic()) {
        CloseLogic();
      }
  } else {
  }
}

void SimpleArb::DoOperationAfterUpdateData(const MarketSnapshot& shot) {
  mid_map[shot.ticker] = (shot.bids[0]+shot.asks[0]) / 2;  // mid_map saved the newest mid, no matter it is aligned or not
  if (strcmp(shot.ticker, hedge_ticker.c_str()) == 0) {
    hedge_ask.push_back(shot.asks[0]);
    hedge_bid.push_back(shot.bids[0]);
    if (hedge_ask.size() > 8) {
      hedge_ask.pop_front();
      hedge_bid.pop_front();
    }
  }
  current_spread = m_shot_map[main_ticker].asks[0] - m_shot_map[main_ticker].bids[0] + m_shot_map[hedge_ticker].asks[0] - m_shot_map[hedge_ticker].bids[0];
  if (IsAlign()) {
    double mid = GetPairMid();
    map_vector.emplace_back(mid);  // map_vector saved the aligned mid, all the elements here are safe to trade
    int num_sample = ++sample_tail - sample_head;
    if (num_sample > train_samples && num_sample % (train_samples) == 1) {
      CalParams();
    }
    // if (m_mode == StrategyMode::Real) {
      printf("%ld [%s, %s]mid_diff is %lf\n", shot.time.tv_sec, main_ticker.c_str(), hedge_ticker.c_str(), mid_map[main_ticker]-mid_map[hedge_ticker]);
    // }
    if (m_ss == StrategyStatus::Training) {
      mean = down_diff = up_diff = stop_loss_down_line = stop_loss_up_line = mid;
    }
    MarketSnapshot shot;
    snprintf(shot.ticker, sizeof(shot.ticker), "['%s', '%s']", main_ticker.c_str(), hedge_ticker.c_str());
    shot.time = m_shot_map[hedge_ticker].time;
    shot.bids[0] = down_diff - current_spread/2;
    shot.bids[1] = stop_loss_down_line;
    shot.bids[2] = mean - current_spread/2;
    shot.asks[0] = up_diff + current_spread/2;
    shot.asks[1] = stop_loss_up_line;
    shot.asks[2] = mean + current_spread/2;
    shot.bids[3] = m_shot_map[main_ticker].bids[0];
    shot.asks[3] = m_shot_map[main_ticker].asks[0];
    shot.bids[4] = m_shot_map[hedge_ticker].bids[0];
    shot.asks[4] = m_shot_map[hedge_ticker].asks[0];
    shot.bid_sizes[3] = m_shot_map[main_ticker].bid_sizes[0];
    shot.ask_sizes[3] = m_shot_map[main_ticker].ask_sizes[0];
    shot.bid_sizes[4] = m_shot_map[hedge_ticker].bid_sizes[0];
    shot.ask_sizes[4] = m_shot_map[hedge_ticker].ask_sizes[0];
    shot.open_interest = mean;
    std::string label = main_ticker + '|' + hedge_ticker;
    snprintf(shot.ticker, sizeof(shot.ticker), "%s", label.c_str());
    shot.last_trade = mid;
    m_ui_sender->Send(shot);
  }
}

void SimpleArb::HandleCommand(const Command& shot) {
  printf("received command!\n");
  shot.Show(stdout);
  if (abs(shot.vdouble[0]) > MIN_DOUBLE_DIFF) {
    up_diff = shot.vdouble[0];
    return;
  }
  if (abs(shot.vdouble[1]) > MIN_DOUBLE_DIFF) {
    down_diff = shot.vdouble[1];
    return;
  }
  if (abs(shot.vdouble[2]) > MIN_DOUBLE_DIFF) {
    stop_loss_up_line = shot.vdouble[2];
    return;
  }
  if (abs(shot.vdouble[3]) > MIN_DOUBLE_DIFF) {
    stop_loss_down_line = shot.vdouble[3];
    return;
  }
}

void SimpleArb::Train() {
}

void SimpleArb::Pause() {
}

void SimpleArb::Resume() {
  sample_head = sample_tail;
}

bool SimpleArb::Ready() {
  int num_sample = sample_tail - sample_head;
  if (m_position_ready && m_shot_map[main_ticker].IsGood() && m_shot_map[hedge_ticker].IsGood() && num_sample >= train_samples) {
    if (num_sample == train_samples) {
      // first cal params
      CalParams();
    }
    return true;
  }
  if (!m_position_ready) {
    printf("waiting position query finish!\n");
  }
  return false;
}

void SimpleArb::ModerateOrders(const std::string & ticker) {
  // just make sure the order filled
  if (m_mode == StrategyMode::Real) {
    for (auto m : m_order_map) {
      Order* o = m.second;
      if (o->Valid()) {
        std::string ticker = o->ticker;
        MarketSnapshot shot = m_shot_map[ticker];
        double reasonable_price = (o->side == OrderSide::Buy ? shot.asks[0] : shot.bids[0]);
        bool is_price_move = (fabs(reasonable_price - o->price) >= min_price_move/2);
        if (!is_price_move) {
          continue;
        }
        if (ticker == main_ticker) {
          if ((o->side == OrderSide::Buy && m_shot_map[hedge_ticker].bids[0] - this->target_hedge_price < -1e-4) ||
          (o->side == OrderSide::Sell && m_shot_map[hedge_ticker].asks[0] - this->target_hedge_price > -1e-4) ) {
            printf("[%s %s]target hedge price is %s@%lf, now is %lf %lf\n", main_ticker.c_str(), hedge_ticker.c_str(), OrderSide::ToString(o->side), target_hedge_price, m_shot_map[hedge_ticker].bids[0], m_shot_map[hedge_ticker].asks[0]);
            CancelOrder(o);
          }
        } else if (ticker == hedge_ticker) {
          // printf("[%s %s]Slip point for :modify %s order %s: %lf->%lf mpv=%lf\n", main_ticker.c_str(), hedge_ticker.c_str(), OrderSide::ToString(o->side), o->order_ref, o->price, reasonable_price, min_price_move);
          /*
          if (m_shot_map[hedge_ticker].time.tv_sec - o->shot_time.tv_sec >= 3) {
            printf("[%s %s] cancel hedge order, bc not filled in 3s\n", main_ticker.c_str(), hedge_ticker.c_str());
            m_shot_map[hedge_ticker].Show(stdout);
            ModOrder(o);
          }
          */
          ModOrder(o);
        } else {
          continue;
        }
      }
    }
  }
}

void SimpleArb::ClearPositionRecord() {
  m_avgcost_map.clear();
  m_position_map.clear();
}

void SimpleArb::Start() {
  if (!is_started) {
    ClearPositionRecord();
    is_started = true;
  }
  Run();
}

void SimpleArb::DoOperationAfterUpdatePos(Order* o, const ExchangeInfo& info) {
}

void SimpleArb::UpdateBound(OrderSide::Enum side) {
  printf("Entering UpdateBound\n");
  int pos = m_position_map[main_ticker];
  if (pos == 0) {  // close operation filled, no update bound
    return;
  }
  if (side == OrderSide::Sell) {
    down_diff = GetPairMid();
    down_diff -= increment;
    if (abs(pos) > 1) {
      mean -= increment/2;
      stop_loss_down_line -= increment/2;
    }
  } else {
    up_diff = GetPairMid();
    up_diff += increment;
    if (abs(pos) > 1) {
      mean += increment/2;
      stop_loss_up_line += increment/2;
    }
  }
  printf("spread is %lf %lf min_profit is %lf, next open will be %lf mean is %lf\n", m_shot_map[main_ticker].asks[0]-m_shot_map[main_ticker].bids[0], m_shot_map[hedge_ticker].asks[0]-m_shot_map[hedge_ticker].bids[0], min_profit, side == OrderSide::Sell ? down_diff: up_diff, mean);
}

void SimpleArb::HandleTestOrder(Order* o) {
  if (m_mode == StrategyMode::Real) {
    return;
  }
  ExchangeInfo info;
  info.shot_time = o->shot_time;
  info.show_time = o->shot_time;
  info.type = InfoType::Filled;
  info.trade_size = o->size;
  info.trade_price = o->price;
  info.side = o->side;
  snprintf(info.order_ref, sizeof(info.order_ref), "%s", o->order_ref);
  snprintf(info.ticker, sizeof(info.ticker), "%s", o->ticker);
  snprintf(info.reason, sizeof(info.reason), "%s", "test");
  // m_position_map[o->ticker] += o->side == OrderSide::Buy ? o->size : -o->size;
  exchange_file->write(reinterpret_cast<char*>(&info), sizeof(info));
  exchange_file->flush();
  // info.Show(stdout);
  UpdatePos(o, info);
  // m_order_map.clear();
  PrintMap(m_position_map);
  PrintMap(m_avgcost_map);
  DoOperationAfterFilled(o, info);
}


void SimpleArb::UpdateBuildPosTime() {
  int hedge_pos = m_position_map[hedge_ticker];
  if (hedge_pos == 0) {  // closed all position, reinitialize build_position_time
    m_build_position_time = MAX_UNIX_TIME;
  } else if (hedge_pos == 1) {  // position 0->1, record build_time
    m_build_position_time = m_tc->TimevalInt(m_last_shot.time);
  }
}

void SimpleArb::RecordPnl(Order* o, bool force_flat) {
  int pos = o->size;
  OrderSide::Enum pos_side = o->side == OrderSide::Sell ? OrderSide::Buy: OrderSide::Sell;
  OrderSide::Enum close_side = o->side;
  double hedge_price = pos > 0 ? m_shot_map[hedge_ticker].asks[0] : m_shot_map[hedge_ticker].bids[0];
  // cout << "main pnl param:" << main_ticker <<" " <<  m_avgcost_map[main_ticker]<< " " <<  abs(pos) << " " << o->price << " " << abs(pos) << endl;
  // cout << "hedge pnl param:" << hedge_ticker <<" " <<  m_avgcost_map[hedge_ticker]<< " " <<  abs(pos) << " " << hedge_price << " " << abs(pos) << endl;
  double this_round_pnl = m_cw->CalNetPnl(main_ticker, m_avgcost_map[main_ticker], abs(pos), o->price, abs(pos), close_side, no_close_today) + m_cw->CalNetPnl(hedge_ticker, m_avgcost_map[hedge_ticker], abs(pos), hedge_price, abs(pos), pos_side, no_close_today);
  /*
  Fee main_fee = m_cal.CalFee(main_ticker, m_avgcost_map[main_ticker], abs(pos), m_shot_map[main_ticker].  bids[0], abs(pos), no_close_today);
  Fee hedge_fee = m_cal.CalFee(hedge_ticker, m_avgcost_map[hedge_ticker], abs(pos), hedge_price, abs(pos), no_close_today);
  double this_round_fee = main_fee.open_fee + main_fee.close_fee + hedge_fee.open_fee + hedge_fee.close_fee;
  */
  std::string str = GetCon(main_ticker);
  std::string split_c = ",";
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%lf", this_round_pnl);
  str += split_c + buffer;
  for (auto i : param_v) {
    snprintf(buffer, sizeof(buffer), "%lf", i);
    str += split_c + buffer;
  }
  str += "\n";
  cout << "recordpnl," << str;
  /*
  printf("%ld [%s %s]%sThis round close pnl: %lf, fee_cost: %lf pos is %d, holding second is %ld, param is ", m_shot_map[hedge_ticker].time.tv_sec, main_ticker.c_str(), hedge_ticker.c_str(), force_flat ? "[Time up] " : "", this_round_pnl, this_round_fee, pos, m_shot_map[hedge_ticker].time.tv_sec - build_position_time);
  for (auto i : param_v) {
    printf("%lf ", i);
  }
  printf("\n");
  */
}

void SimpleArb::DoOperationAfterFilled(Order* o, const ExchangeInfo& info) {
  if (strcmp(o->ticker, main_ticker.c_str()) == 0) {
    // get hedged right now
    std::string a = o->tbd;
    if (a.find("close") != string::npos) {
      close_round++;
      RecordPnl(o);
      CalParams();
    } else {
    }
    // std::string oc = (m_position_map[hedge_ticker] == 0 ? "open" : "close");
    OrderSide::Enum hedge_side = (o->side == OrderSide::Buy) ? OrderSide::Sell : OrderSide::Buy;
    Order* order = NewOrder(hedge_ticker, hedge_side, info.trade_size, false, false, o->tbd, no_close_today);
    RecordSlip(hedge_ticker, hedge_side, a.find("close") != string::npos);
    HandleTestOrder(order);
    order->Show(stdout);
  } else if (strcmp(o->ticker, hedge_ticker.c_str()) == 0) {
    UpdateBuildPosTime();
    UpdateBound(o->side);
  } else {
    printf("o->ticker=%s, main:%s, hedge:%s\n", o->ticker, main_ticker.c_str(), hedge_ticker.c_str());
    SimpleHandle(322);
  }
}

bool SimpleArb::Spread_Good() {
  return (current_spread > spread_threshold) ? false : true;
}
