// T21 (see /Users/geekjapan/dev/projects/touhou/PLAN.md's M5) acceptance
// test for the OP.EXE -> REIIDEN.EXE binary-switching chain.
//
// Unlike every other *_test.cpp in this directory, this test does NOT call
// main_op()/main_main() in-process. port/shim/masterlib/chain_exec.cpp
// makes game_switch_binary() a real execv() into a sibling *process* --
// once that fires, this test's own process image would be replaced too if
// it called main_op() directly, taking this file's code with it. So
// instead this test fork()s, execve()s the real th01op executable in the
// child with two autopilot environment variables set (see
// port/app/chain_autopilot.hpp and the TH01_PORT_AUTOPILOT_OP/
// TH01_PORT_AUTOPILOT_MAIN-gated threads in main_op.cpp/main_reiiden.cpp),
// and waits for that whole process chain -- op -> (execv) -> main -> stage
// 1 x4 accelerated card-wall clears -> (execv) -> a fresh main instance at
// the boss-adjacent stage_id -- to run unattended across real OS process
// boundaries and exit(0) on its own.
//
// Verifies two things a naive "it didn't crash" check would miss:
//  (a) the switch is a *real* execv(), not a no-op/stub: chain_exec.cpp's
//      sibling-binary resolution and TH01_PORT_STATE_FILE hand-off both
//      have to work for the child to progress at all (menu_nav_test.cpp's
//      September predecessor never drove START, since the old
//      entrypoint_exec() stub just logged and exit(0)'d).
//  (b) resident state actually survives two hops of it: main_op.cpp's
//      autopilot writes a sentinel to resident_t::unused_3 (a field no
//      original ZUN code ever touches) before pressing START; main_reiiden.
//      cpp's autopilot reads it back, in the *second* th01main process
//      instance, and reports it via a marker file this test then checks.
//
// Skipped (not failed) when the real TH01 assets aren't available, matching
// title_screen_golden_test's convention.

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "port/app/assets_dir.hpp"

namespace {

constexpr const char *DEFAULT_ASSETS_DIR =
	"/Users/geekjapan/dev/projects/touhou/assets/th01";
constexpr const char *ASSET_CANARY = "REIIDEN2.GRP";

constexpr int EXIT_SKIP = 77;
constexpr uint32_t SENTINEL = 0x5eed1234u;

// Safety net for a hung/misbehaving chain: ctest itself has a TIMEOUT
// property, but that only kills *this* process, potentially orphaning the
// forked child (and whatever it's since execv()'d into). A background
// watchdog thread kills the child directly -- execv() never changes the
// PID, so a single kill() reaches whichever binary is currently running.
constexpr int WATCHDOG_TIMEOUT_MS = 90000;

} // namespace

int main() {
	std::string assets_dir = port::resolve_assets_dir(
		std::getenv("TH01_ASSETS_DIR"), DEFAULT_ASSETS_DIR, ASSET_CANARY
	);
	std::string canary_path = assets_dir + "/" + ASSET_CANARY;
	if(!port::file_exists(canary_path)) {
		std::printf(
			"fullchain_binary_switch: skipping -- no real TH01 assets at "
			"'%s' (set TH01_ASSETS_DIR to enable this test)\n",
			assets_dir.c_str()
		);
		return EXIT_SKIP;
	}

	std::string op_path = std::string(PORT_CHAIN_BIN_DIR) + "/th01op";
	std::string marker_path =
		std::string(PORT_TESTS_TMP_DIR) + "/fullchain_marker.txt";
	std::string state_path =
		std::string(PORT_TESTS_TMP_DIR) + "/fullchain_state.bin";
	::unlink(marker_path.c_str());
	::unlink(state_path.c_str());

	pid_t pid = fork();
	if(pid < 0) {
		std::perror("fullchain_binary_switch: fork");
		return 1;
	}
	if(pid == 0) {
		if(::chdir(assets_dir.c_str()) != 0) {
			std::perror("fullchain_binary_switch: chdir");
			_exit(1);
		}
		setenv("TH01_ASSETS_DIR", assets_dir.c_str(), 1);
		setenv("TH01_PORT_BIN_DIR", PORT_CHAIN_BIN_DIR, 1);
		setenv("TH01_PORT_STATE_FILE", state_path.c_str(), 1);
		setenv("TH01_PORT_AUTOPILOT_OP", "1", 1);
		setenv("TH01_PORT_AUTOPILOT_MAIN", "1", 1);
		setenv("TH01_PORT_CHAIN_MARKER", marker_path.c_str(), 1);
		execl(op_path.c_str(), "op", static_cast<char *>(nullptr));
		std::perror("fullchain_binary_switch: execl(th01op)");
		_exit(1);
	}

	std::mutex m;
	std::condition_variable cv;
	bool done = false;

	std::thread watchdog([&] {
		std::unique_lock<std::mutex> lock(m);
		cv.wait_for(
			lock, std::chrono::milliseconds(WATCHDOG_TIMEOUT_MS),
			[&] { return done; }
		);
		if(!done) {
			std::fprintf(
				stderr,
				"fullchain_binary_switch: watchdog -- chain exceeded %dms, "
				"killing pid %d\n",
				WATCHDOG_TIMEOUT_MS, static_cast<int>(pid)
			);
			kill(pid, SIGKILL);
		}
	});

	int status = 0;
	pid_t waited = waitpid(pid, &status, 0);

	{
		std::lock_guard<std::mutex> lock(m);
		done = true;
	}
	cv.notify_all();
	watchdog.join();

	if(waited != pid) {
		std::perror("fullchain_binary_switch: waitpid");
		return 1;
	}
	if(!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		std::fprintf(
			stderr,
			"fullchain_binary_switch: FAIL -- chain process exited "
			"abnormally (raw status=%d)\n",
			status
		);
		return 1;
	}

	FILE *f = std::fopen(marker_path.c_str(), "r");
	if(f == nullptr) {
		std::fprintf(
			stderr,
			"fullchain_binary_switch: FAIL -- no marker file at '%s' -- the "
			"chain exited cleanly but never reported a verified transition\n",
			marker_path.c_str()
		);
		return 1;
	}
	unsigned stage_id = 0;
	unsigned rank = 0;
	int rem_lives = 0;
	unsigned long score = 0;
	unsigned sentinel = 0;
	int n = std::fscanf(
		f, "stage_id=%u rank=%u rem_lives=%d score=%lu sentinel=0x%x",
		&stage_id, &rank, &rem_lives, &score, &sentinel
	);
	std::fclose(f);
	if(n != 5) {
		std::fprintf(
			stderr,
			"fullchain_binary_switch: FAIL -- marker file at '%s' is "
			"malformed (parsed %d/5 fields)\n",
			marker_path.c_str(), n
		);
		return 1;
	}
	if(sentinel != SENTINEL) {
		std::fprintf(
			stderr,
			"fullchain_binary_switch: FAIL -- resident sentinel did not "
			"survive the exec chain (got 0x%x, want 0x%x)\n",
			sentinel, SENTINEL
		);
		return 1;
	}

	std::printf(
		"fullchain_binary_switch: OP menu -> START -> execv() -> stage 1 "
		"(4 accelerated card-wall clears) -> execv() all verified across "
		"real OS processes; stage_id=%u rank=%u rem_lives=%d score=%lu, "
		"resident sentinel 0x%x survived both execv() hops\n",
		stage_id, rank, rem_lives, score, sentinel
	);
	return 0;
}
