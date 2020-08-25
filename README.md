

Setting hot mode..
Node 1, batt value = 1448
Node 2, batt value = 1558
Node 3, batt value = 1431
Node 4 send error
Node 5 send error

Setting stop mode..
Node 1, batt value = 1437
Node 2, batt value = 1558
Node 3, batt value = 1417
Node 4 send error
Node 5 send error


Keep statistics button pressed:
Packets sent: 4
1 - batt=1378 wins=0 lateWins=0, errHot=0 errStop=0 noacks=0
2 - batt=1558 wins=0 lateWins=0, errHot=0 errStop=0 noacks=0
3 - batt=1440 wins=0 lateWins=0, errHot=0 errStop=0 noacks=0
Node 1, batt value = 1462
Node 2, batt value = 1558
Node 3, batt value = 1503
Node 4 send error
Node 5 send error
Packets sent: 5
1 - batt=1462 wins=0 lateWins=0, errHot=0 errStop=0 noacks=0
2 - batt=1558 wins=0 lateWins=0, errHot=0 errStop=0 noacks=0
3 - batt=1503 wins=0 lateWins=0, errHot=0 errStop=0 noacks=0
Node 1, batt value = 1378
Node 2, batt value = 1558
Node 3, batt value = 1484
Node 4 send error
Node 5 send error

--> all statistic error counters should stay close to 0


Running test right after boot:
Sending test mode to nodes: no active nodes, activate nodes first.

Running test normal:

Sending test mode to nodes: 312
Node win received = 1		--> node 1 is winning
Node 1, batt value = 1500
Node 2, batt value = 1558
Node 3 send error   --> this is a collission because 3 also tried to win right after 1
Node 4 send error
Node 5 send error
Sending test mode to nodes: 123
Node win received = 2		--> node 2 is winning and none of the others were ready to win
Node 1, batt value = 1318
Node 2, batt value = 1558
Node 3, batt value = 1507
Node 4 send error
Node 5 send error
Sending test mode to nodes: 231
Node win received = 3
Node 1, batt value = 1459
Node 2, batt value = 1558
Node 3, batt value = 1506
Node 4 send error
Node 5 send error

Statistics button:

Packets sent: 963
1 - batt=1459 wins=176 lateWins=0, errHot=0 errStop=1 noacks=1
2 - batt=1558 wins=170 lateWins=0, errHot=0 errStop=20 noacks=10
3 - batt=1506 wins=132 lateWins=0, errHot=0 errStop=177 noacks=19

ErrHot should be close to 0


http://soundbible.com/suggest.php?q=bell&x=0&y=0


