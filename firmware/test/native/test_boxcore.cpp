// Host test for the standalone box clocks.
// Build: g++ -std=c++17 -Wall -Ilib/boxcore test/native/test_boxcore.cpp lib/boxcore/boxcore.cpp -o /tmp/tb && /tmp/tb
#include "boxcore.h"
#include <cstdio>

using namespace boxcore;

static int failures = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);          \
            failures++;                                                       \
        }                                                                     \
    } while (0)

static void servesAndStaysUntilReleased() {
    BoxTimer b;
    uint8_t id = b.sit(1000);
    CHECK(id && b.count() == 1);
    const Clock* c = b.find(id);
    CHECK(b.remaining(*c, 1000) == 30000 && b.phase(*c, 1000) == Phase::Serving);
    CHECK(b.remaining(*c, 21000) == 10000 && b.phase(*c, 21000) == Phase::Stand);
    CHECK(b.phase(*c, 31000) == Phase::Done);
    CHECK(b.remaining(*c, 35000) == -4000); // never released by itself
    CHECK(b.count() == 1);
    CHECK(b.release(id, 36000) && b.count() == 0);
}

static void pauseStopsAllClocks() {
    BoxTimer b;
    uint8_t a = b.sit(0);
    uint8_t c = b.sit(5000);
    b.setPaused(true, 10000);
    CHECK(b.paused());
    CHECK(b.remaining(*b.find(a), 60000) == 20000);
    CHECK(b.remaining(*b.find(c), 60000) == 25000);
    uint8_t d = b.sit(61000); // sitting while paused: waits too
    CHECK(b.remaining(*b.find(d), 70000) == 30000);
    b.setPaused(false, 70000);
    CHECK(b.remaining(*b.find(a), 75000) == 15000);
    CHECK(b.remaining(*b.find(d), 75000) == 25000);
}

static void plusPenaltyAndCancel() {
    BoxTimer b;
    uint8_t id = b.sit(0);
    CHECK(b.addPenalty(id, 10000));
    CHECK(b.remaining(*b.find(id), 10000) == 50000);
    CHECK(b.find(id)->penalties == 2);
    CHECK(b.cancel(id, 11000) && b.count() == 0);
    CHECK(!b.addPenalty(id, 12000));
}

static void undoKeepsTimeRunning() {
    Config cfg;
    cfg.undoLevels = 2;
    BoxTimer b(cfg);
    uint8_t id = b.sit(0);
    b.release(id, 20000);
    CHECK(b.count() == 0);
    CHECK(b.undo(25000)); // released by mistake: back, and it kept serving
    CHECK(b.count() == 1 && b.remaining(*b.find(id), 25000) == 5000);
    CHECK(b.undo(26000)); // undo the sit
    CHECK(b.count() == 0 && !b.canUndo());
}

static void jammerSwap() {
    BoxTimer b;
    uint8_t left = b.sit(0, 0);
    CHECK(b.sit(1000, 0) == 0); // the left jammer is already in
    uint8_t right = b.sit(5000, 1);
    const Clock* l = b.find(left);
    const Clock* r = b.find(right);
    // left had 25 s left: both shorten by 25 s
    CHECK(b.remaining(*l, 5000) == 0 && b.phase(*l, 5000) == Phase::Done);
    CHECK(b.remaining(*r, 5000) == 5000);
    CHECK(r->shortened == 1 && l->shortened == 1);
    // left released, right gets another penalty: no new swap, 30 s more
    b.release(left, 6000);
    r = b.find(right);
    CHECK(b.addPenalty(right, 7000));
    CHECK(b.remaining(*r, 7000) == 33000);
}

static void jammerSwapWithSecondPenaltyWhileBothSit() {
    BoxTimer b;
    uint8_t left = b.sit(0, 0);
    uint8_t right = b.sit(4000, 1); // left 26 s left -> left done, right 4 s
    CHECK(b.remaining(*b.find(right), 4000) == 4000);
    // left gets a new penalty while right still sits: as in CRG no second
    // swap, because right's only penalty has already been shortened
    CHECK(b.addPenalty(left, 6000));
    const Clock* l = b.find(left);
    const Clock* r = b.find(right);
    CHECK(b.remaining(*r, 6000) == 2000);
    CHECK(b.remaining(*l, 6000) == 28000);
}

static void fullAndClear() {
    BoxTimer b;
    for (int i = 0; i < kMaxClocks; i++) CHECK(b.sit(i * 1000));
    CHECK(b.sit(9000) == 0);
    b.clear();
    CHECK(b.count() == 0 && !b.canUndo());
}

int main() {
    servesAndStaysUntilReleased();
    pauseStopsAllClocks();
    plusPenaltyAndCancel();
    undoKeepsTimeRunning();
    jammerSwap();
    jammerSwapWithSecondPenaltyWhileBothSit();
    fullAndClear();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all boxcore tests passed\n");
    return 0;
}
