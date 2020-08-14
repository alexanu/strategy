#ifndef STRATEGY_SIMPLEMAKER_SIMPLEMAKER_H_
#define STRATEGY_SIMPLEMAKER_SIMPLEMAKER_H_

#include <unordered_map>

#include <cmath>
#include <vector>
#include <string>

#include "struct/market_snapshot.h"
#include "struct/strategy_status.h"
#include "util/time_controller.h"
#include "struct/order.h"
#include "util/zmq_sender.hpp"
#include "struct/exchange_info.h"
#include "struct/order_status.h"
#include "util/common_tools.h"
#include "core/base_strategy.h"


class SimpleMaker : public BaseStrategy {
 public:
  explicit SimpleMaker(const std::string & main_ticker, const std::string & hedge_ticker, int maxpos, double tick_size, TimeController tc, int contract_size, const std::string & strat_name, std::unordered_map<std::string, std::vector<BaseStrategy*> >*ticker_strat_map, bool enable_stdout = true, bool enable_file = true);
  ~SimpleMaker();

  void Start() override;
  void Stop() override;
  void Flatting() override;

 private:
  void DoOperationAfterUpdatePos(Order* o, const ExchangeInfo& info) override;
  void DoOperationAfterUpdateData(const MarketSnapshot& shot) override;
  void DoOperationAfterFilled(Order* o, const ExchangeInfo& info) override;
  void DoOperationAfterCancelled(Order* o) override;
  double OrderPrice(const std::string & contract, OrderSide::Enum side, bool control_price) override;

  bool Ready() override;
  void Run() override;
  void Train() override;
  void Resume() override;
  void Pause() override;

  bool IsHedged();
  bool MidBuy();
  bool IsAlign();
  bool MidSell();

  bool IsParamOK();
  bool Spread_Good();
  void ModerateHedgeOrders();
  void ModerateOrders(const std::string & contract, double edurance);
  // void ModerateOrders(const std::string & contract);

  double CalBalancePrice();

  bool TradeClose(const std::string & contract, int size);

  bool PriceChange(double current_price, double reasonable_price, OrderSide::Enum side, double edurance);

  void AddCloseOrderSize(OrderSide::Enum side);
  void CheckStatus();
  void ModerateAllValid(const std::string & contract, OrderSide::Enum side);

  void ModerateOrders(const std::string & contract) override;

  void OpenOrder(OrderSide::Enum sd, const std::string & info);
  char order_ref[MAX_ORDERREF_SIZE];
  std::string main_ticker;
  std::string hedge_ticker;
  int start_pos;
  double poscapital;
  double min_price;
  double price_control;
  double edurance;
  TimeController this_tc;

  pthread_mutex_t add_size_mutex;
  int cancel_threshhold;
  std::unordered_map<std::string, double> mid_map;
  std::unordered_map<std::string, Order*> sleep_order_map;
  double up_diff;
  double down_diff;
  std::vector<double> map_vector;
  double max_spread;
  unsigned int min_train_sample;
  int max_pos;
};

#endif  // STRATEGY_SIMPLEMAKER_SIMPLEMAKER_H_
