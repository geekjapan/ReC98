// port/app/host
// -------------
// SDL3 presentation host shared by every port/app/ entry point. Owns the
// window/renderer/streaming-texture triple and the "present one frame" step
// that th01/hardware/vsync.hpp's REC98_PORT branch calls into (see that
// file's header comment) to turn ReC98's original vsync-interrupt busy-waits
// into a real, frame-paced blocking wait -- no coroutine or background
// thread required, matching SPEC.md's app/host design.

#ifndef PORT_APP_HOST_HPP
#define PORT_APP_HOST_HPP

namespace port {

// Initializes SDL3 video, opens a window showing the PC-98 640x400 logical
// resolution (integer-scaled, letterboxed), and creates the streaming
// texture host_present_frame() renders into. Returns false (after printing
// an SDL error to stderr) on failure; safe to call host_shutdown() either
// way afterwards.
bool host_init(const char *window_title);

// Tears down the SDL texture/renderer/window and calls SDL_Quit(). Safe to
// call even if host_init() failed or was never called.
void host_shutdown();

// Pumps SDL events, feeds the live keyboard state into the port::input_poll()
// shim (port/shim/input.cpp) for this frame, renders the current pc98emu
// VRAM contents, and presents -- blocking until vsync (or ~1/60s via
// SDL_Delay if SDL_SetRenderVSync() isn't honored by the backend).
//
// If the user closes the window (SDL_EVENT_QUIT), this calls host_shutdown()
// and exits the process directly: there's no portable way to unwind out of
// ReC98's decades-old, deeply nested game loops from here, and every caller
// (frame_delay()'s busy-wait, z_vsync_wait()) has no return value to thread
// a "please quit now" signal through anyway.
void host_present_frame();

} // namespace port

#endif // PORT_APP_HOST_HPP
