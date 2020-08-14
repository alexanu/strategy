#ifndef STRATEGY_SIMPLEARB2_SIMPLEARB2_H_
#define STRATEGY_SIMPLEARB2_SIMPLEARB2_H_

#include <unordered_map>

#include <cmath>
#include <vector>
#include <string>
#include <iostream>
#include <deque>
#include <memory>

#include <libconfig.h++>

#include "struct/market_snapshot.h"
#include "struct/strategy_status.h"
#include "struct/strategy_mode.h"
#include "struct/order.h"
#include "struct/command.h"
#include "struct/exchange_info.h"
#include "struct/order_status.h"
#include "util/time_controller.h"
#include "util/zmq_sender.hpp"
#include "util/dater.h"
#include "util/history_worker.h"
#include "util/contract_worker.h"
#include "util/common_tools.h"
#include "core/base_strategy.h"

class SimpleArb2 : public BaseStrategy {
 public:
  explicit SimpleArb2(const libconfig::Setting & param_setting, std::unordered_map<std::string, std::vector<BaseStrategy*> >*ticker_strat_map, BaseSender<MarketSnapshot>* uisender, BaseSender<Order>* ordersender, TimeController* tc, ContractWorker* cw, HistoryWorker* hw, const std::string & date, StrategyMode::Enum mode = StrategyMode::Real, std::ofstream* exchange_file = nullptr);
  ~SimpleArb2();

  void Start() override;
  void Stop() override;

 private:
  bool FillStratConfig(const libconfig::Setting& param_setting);
  void RunningSetup(std::unordered_map<std::string, std::vector<BaseStrategy*> >*ticker_strat_map, BaseSender<MarketSnapshot>* uisender, BaseSender<Order>* ordersender);
  void DoOperationAfterUpdateData(const MarketSnapshot& shot) override;
  void DoOperationAfterFilled(Order* o, const ExchangeInfo& info) override;
  void DoOperationAfterCancelled(Order* o) override;
  void ModerateOrders(const std::string & contract) override;

  bool Ready() override;
  void Resume() override;
  void Run() override;
  void Flatting() override;

  double OrderPrice(const std::string & contract, OrderSide::Enum side, bool control_price) override;

  bool OpenLogic();
  void CloseLogic();
  void SoftCloseLogic();

  void Open(OrderSide::Enum side);
  bool Close(OrderSide::Enum side);

  void ForceFlat() override;

  bool Spread_Good();

  bool IsAlign();

  // bool NewHigh(OrderSide::Enum side);
  void UpdateParams(const std::string& tag = "");

  // strategy core param
  std::string date_;
  std::string main_ticker_;
  std::string hedge_ticker_;
  int max_close_try_;

  // realtime update param
  double current_spread_;
  int close_round_;
  int sample_head_;
  int sample_tail_;
  double target_hedge_price_;
  std::vector<double> mids_;

  // read from config
  int max_pos_;
  double min_price_move_;
  int cancel_limit_;
  double min_profit_;
  int train_samples_;
  double min_range_;
  double range_width_;
  double spread_threshold_;
  bool no_close_today_;
  int max_round_;

  // strategy parameter
  double up_diff_;
  double down_diff_;
  double mean_;

  std::ofstream* exchange_file_;
};

#endif  // STRATEGY_SIMPLEARB2_SIMPLEARB2_H_
