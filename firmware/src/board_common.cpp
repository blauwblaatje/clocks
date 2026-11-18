// Shared by the board files.
#include "board.h"

namespace board {

// Single-cell LiPo at light load: rough voltage-to-charge table.
int lipoPercent(int mv) {
    static const int table[][2] = {{4180, 100}, {4100, 90}, {4000, 80}, {3920, 70}, {3850, 60}, {3800, 50},
                                   {3750, 40}, {3710, 30}, {3670, 20}, {3610, 10}, {3500, 5},  {3300, 0}};
    if (mv >= table[0][0]) return 100;
    for (size_t i = 1; i < sizeof table / sizeof table[0]; i++) {
        if (mv >= table[i][0]) {
            int v0 = table[i][0], v1 = table[i - 1][0], p0 = table[i][1], p1 = table[i - 1][1];
            return p0 + (mv - v0) * (p1 - p0) / (v1 - v0);
        }
    }
    return 0;
}

} // namespace board
