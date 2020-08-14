#include <iostream>
#include <vector>
#include <string>

#include "./demostrat.h"

DemoStrat::DemoStrat(std::unordered_map<std::string, std::vector<BaseStrategy*> >*ticker_strat_map, ZmqSender<Order>* ordersender, TimeController* tc) {
  m_strat_name = "coin";
  m_order_sender = ordersender;
  m_tc = tc;
  main_ticker = "ETH-USDT-SWAP";
  hedge_ticker = "asd";
  (*ticker_strat_map)[main_ticker].emplace_back(this);
  (*ticker_strat_map)[hedge_ticker].emplace_back(this);
  (*ticker_strat_map)["positionend"].emplace_back(this);
}

DemoStrat::~DemoStrat() {
}

bool DemoStrat::Ready() {
  return true;
}

void DemoStrat::Run() {
}

void DemoStrat::DoOperationAfterCancelled(Order* o) {
  printf("ticker %s cancel num %d!\n", o->ticker, m_cancel_map[o->ticker]);
  if (m_cancel_map[o->ticker] > 100) {
    printf("ticker %s hit cancel limit!\n", o->ticker);
    // Stop();
  }
}

double DemoStrat::OrderPrice(const std::string & ticker, OrderSide::Enum side, bool control_price) {
  // this is a logic to make order use market price
  // return (side == OrderSide::Buy)?m_shot_map[ticker].asks[0]:m_shot_map[ticker].bids[0];
  return (side == OrderSide::Buy)? 350:1000;
}

void DemoStrat::Start() {
  // int pos = position_map[];
  // start with two order
  // NewOrder(, OrderSide::Buy, 1, false, false);
  // NewOrder(main_ticker, OrderSide::Sell, 1000, false, false, "");
  PlaceOrder(main_ticker, 392, 1, false, "test")->Show(stdout);
  // o->Show(stdout);
}

void DemoStrat::DoOperationAfterUpdateData(const MarketSnapshot& shot) {
  // shot.Show(stdout);
}

void DemoStrat::ModerateOrders(const std::string & ticker) {
  for (std::unordered_map<std::string, Order*>::iterator it = m_order_map.begin(); it != m_order_map.end(); it++) {
    Order* o = it->second;
    MarketSnapshot shot = m_shot_map[o->ticker];
    if (o->Valid()) {
      if (o->side == OrderSide::Buy && fabs(o->price - shot.asks[0]) > 0.01) {
        // ModOrder(o);
      } else if (o->side == OrderSide::Sell && fabs(o->price - shot.bids[0]) > 0.01) {
        // ModOrder(o);
      } else {
      }
    }
  }
}

void DemoStrat::DoOperationAfterUpdatePos(Order* o, const ExchangeInfo& info) {
}

void DemoStrat::DoOperationAfterFilled(Order* o, const ExchangeInfo& info) {
}
