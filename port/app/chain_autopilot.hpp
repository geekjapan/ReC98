// port/app/chain_autopilot
// -------------------------
// T21 (see /Users/geekjapan/dev/projects/touhou/PLAN.md's M5) binary-
// switching acceptance test support.
//
// port/shim/masterlib/chain_exec.cpp makes game_switch_binary() a real
// execv() into a sibling *process* -- so a normal in-process test director
// thread (menu_nav_test.cpp's pattern, driving synthetic key state and
// polling game globals from a second thread) cannot survive the hop: the
// whole process image, all its threads included, is replaced the instant
// entrypoint_exec() runs. There is no way for an external harness to inject
// synthetic keypresses into a process it didn't fork itself, and it can't
// fork/exec each hop itself either, since it doesn't control *when*
// game_switch_binary() decides to switch.
//
// So the only place a scripted "play through this part unattended" driver
// can live is inside the production binaries themselves. main_op.cpp and
// main_reiiden.cpp each gain a small, off-by-default (TH01_PORT_AUTOPILOT_OP
// / TH01_PORT_AUTOPILOT_MAIN environment variables) autopilot thread doing
// exactly what the equivalent *_test.cpp file already proved works
// in-process (menu_nav_test.cpp's Down/Up/Return sequence; stage1_holdshot_
// test.cpp's held-Shot + test_damage cheat), so it keeps working across a
// real execv() -- the environment variable travels automatically with the
// chain, and each freshly-exec'd process re-enables its own autopilot the
// same way.
//
// CHAIN_TEST_SENTINEL is written to resident_t::unused_3 (th01/resident.hpp
// -- a field no original ZUN code ever reads or writes) by main_op.cpp's
// autopilot right before pressing the START menu's OK, and read back by
// main_reiiden.cpp's autopilot once it observes resident_t::stage_id at
// BOSS_STAGE. Surviving that round trip is the actual thing being tested:
// a leftover in-process pointer proves nothing, but this value can only be
// read back correctly if it was serialized to the TH01_PORT_STATE_FILE
// hand-off file, execv()'d across (twice: OP->MAIN, then MAIN's own
// self-re-exec for the boss-adjacent stage_id transition), and deserialized
// by a brand new process image.

#ifndef PORT_APP_CHAIN_AUTOPILOT_HPP
#define PORT_APP_CHAIN_AUTOPILOT_HPP

#include <cstdint>

namespace port {

constexpr uint32_t CHAIN_TEST_SENTINEL = 0x5eed1234u;

// Environment variable naming the file port/tests/fullchain_binary_switch_
// test.cpp reads back after the whole chain exits, to confirm state
// actually survived (see main_reiiden.cpp's autopilot).
constexpr const char *CHAIN_TEST_MARKER_ENV = "TH01_PORT_CHAIN_MARKER";

} // namespace port

#endif // PORT_APP_CHAIN_AUTOPILOT_HPP
