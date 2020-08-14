#ifndef STRATEGY_DEMOSTRAT_DEMOSTRAT_H_
#define STRATEGY_DEMOSTRAT_DEMOSTRAT_H_

#include <unordered_map>

#include <cmath>
#include <vector>
#include <string>

#include "struct/market_snapshot.h"
#include "util/time_controller.h"
#include "struct/order.h"
#include "util/zmq_sender.hpp"
#include "struct/exchange_info.h"
#include "struct/order_status.h"
#include "util/common_tools.h"
#include "core/base_strategy.h"

class DemoStrat : public BaseStrategy {
 public:
  explicit DemoStrat(std::unordered_map<std::string, std::vector<BaseStrategy*> >*ticker_strat_map, ZmqSender<Order>* ordersender, TimeController* tc);
  ~DemoStrat();

 private:
  // not must realize, but usually, it should
  void DoOperationAfterUpdatePos(Order* o, const ExchangeInfo& info) override;
  void DoOperationAfterUpdateData(const MarketSnapshot& shot) override;
  void DoOperationAfterFilled(Order* o, const ExchangeInfo& info) override;
  void DoOperationAfterCancelled(Order* o) override;

  // not must
  void ModerateOrders(const std::string & contract) override;

  void Start() override;
  bool Ready() override;
  void Run() override;

  // must realize, define the order price logic when send an order
  double OrderPrice(const std::string & contract, OrderSide::Enum side, bool control_price) override;

  std::string main_ticker;
  std::string hedge_ticker;
};

#endif  // STRATEGY_DEMOSTRAT_DEMOSTRAT_H_
