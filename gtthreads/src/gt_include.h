#ifndef __GT_INCLUDE_H
#define __GT_INCLUDE_H

#include "gt_signal.h"
#include "gt_spinlock.h"
#include "gt_tailq.h"
#include "gt_bitops.h"

#include "gt_uthread.h"
#include "gt_pq.h"
#include "gt_kthread.h"

#endif

#define U_DEBUG 0

/* 
********************************
MATRIX 32 

MATRIX 64 

MATRIX 128 

MATRIX 256 
********************************
0. runtime: 930, wait time: 4872
1. runtime: 941, wait time: 4073
2. runtime: 938, wait time: 3375
3. runtime: 928, wait time: 2508
4. runtime: 2420, wait time: 18477
5. runtime: 5765, wait time: 15923
6. runtime: 4150, wait time: 13233
7. runtime: 2732, wait time: 7720
8. runtime: 7985, wait time: 41327
9. runtime: 4235, wait time: 35396
10. runtime: 4265, wait time: 31135
11. runtime: 6007, wait time: 26700
12. runtime: 14237, wait time: 91430
13. runtime: 13018, wait time: 79716
14. runtime: 13134, wait time: 66459
15. runtime: 13572, wait time: 54756

*/