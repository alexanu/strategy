#ifndef STRATEGY_PAIRTRADING_PAIRTRADING_H_
#define STRATEGY_PAIRTRADING_PAIRTRADING_H_

#include <unordered_map>

#include <cmath>
#include <vector>
#include <string>
#include <iostream>
#include <memory>

#include <libconfig.h++>

#include "util/time_controller.h"
#include "util/zmq_sender.hpp"
#include "util/history_worker.h"
#include "util/contract_worker.h"
#include "util/common_tools.h"
#include "util/dater.h"

#include "struct/exchange_info.h"
#include "struct/order_status.h"
#include "struct/strategy_mode.h"
#include "struct/order.h"
#include "struct/command.h"
#include "struct/market_snapshot.h"
#include "struct/strategy_status.h"

#include "core/base_strategy.h"

class PairTrading : public BaseStrategy {
 public:
  explicit PairTrading(const libconfig::Setting & param_setting, std::unordered_map<std::string, std::vector<BaseStrategy*> >*ticker_strat_map, ZmqSender<MarketSnapshot>* uisender, ZmqSender<Order>* ordersender, TimeController* tc, ContractWorker* cw, const std::string & date, StrategyMode::Enum mode = StrategyMode::Real, std::ofstream* exchange_file = nullptr);
  ~PairTrading();


 private:
  bool FillStratConfig(const libconfig::Setting& param_setting);
  void RunningSetup(std::unordered_map<std::string, std::vector<BaseStrategy*> >*ticker_strat_map, ZmqSender<MarketSnapshot>* uisender, ZmqSender<Order>* ordersender);
  void ClearPositionRecord();
  void DoOperationAfterUpdateData(const MarketSnapshot& shot) override;
  void DoOperationAfterUpdatePos(Order* o, const ExchangeInfo& info) override;
  void DoOperationAfterFilled(Order* o, const ExchangeInfo& info) override;
  void DoOperationAfterCancelled(Order* o) override;
  void ModerateOrders(const std::string & contract) override;

  void Start() override;
  void Stop() override;
  bool Ready() override;
  void Resume() override;
  void Run() override;
  void Flatting() override;

  double OrderPrice(const std::string & contract, OrderSide::Enum side, bool control_price) override;

  OrderSide::Enum OpenLogicSide();
  bool OpenLogic();
  void CloseLogic();

  void Open(OrderSide::Enum side);
  bool Close(OrderSide::Enum side);

  void RecordSlip(const std::string & ticker, OrderSide::Enum side, bool is_close = false);
  void RecordPnl(Order* o, bool force_flat = false);

  void CalParams();

  void ForceFlat() override;

  bool Spread_Good();

  bool IsAlign();

  bool RiskCheck();

  std::string main_ticker;
  std::string hedge_ticker;
  int max_pos;
  double min_price_move;

  // std::unordered_map<std::string, std::vector<BaseStrategy*> >*tsm;
  int cancel_limit;
  std::unordered_map<std::string, double> mid_map;
  double up_diff;
  double down_diff;
  double range_width;
  double mean;
  std::vector<double> map_vector;
  int current_pos;
  double min_profit;
  int train_samples_;
  double min_range;
  double increment;
  std::string date;
  double spread_threshold;
  int closed_size;
  double last_valid_mid;
  double stop_loss_up_line;
  double stop_loss_down_line;
  int max_loss_times;
  double stop_loss_times;
  double stop_loss_margin;
  int max_close_try;
  double current_spread;
  bool is_started;
  bool no_close_today;
  // int open_count;
  // int close_count;
  int max_round;
  int close_round;
  int split_num;
  std::vector<double> param_v;
  int sample_head;
  int sample_tail;
  std::ofstream* exchange_file;
  std::vector<double> long_;
  std::vector<double> short_;
  double long_up_;
  double long_down_;
  double long_mean_;
  double short_up_;
  double short_down_;
  double short_mean_;
  double target_hedge_price;
};

#endif  // STRATEGY_PAIRTRADING_PAIRTRADING_H_
