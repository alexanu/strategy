WAF = ../backend/tools/waf-light

all:
	$(WAF) configure all $(PARAMS)

simplemaker:
	$(WAF) configure simplemaker $(PARAMS)

simplearb:
	$(WAF) configure simplearb $(PARAMS)

simplearb2:
	$(WAF) configure simplearb2 $(PARAMS)

coinarb:
	$(WAF) configure coinarb $(PARAMS)

pairtrading:
	$(WAF) configure pairtrading $(PARAMS)

demostrat:
	$(WAF) configure demostrat $(PARAMS)

clean:
	rm -rf build
