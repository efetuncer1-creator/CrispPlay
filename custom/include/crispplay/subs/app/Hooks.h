// Game subtitle translation: the two extension points upstream files call
// (VEYRA_CUSTOM hooks).
//
//   src/ui/bridge/Subtitles.cpp  tickSubtitles(): appendLines(draw, view, client)
//   apps/veyra-qml/main.cpp      installQml(engine, &bridge, dataDirectory)
//
// appendLines is a header-only dispatch to a backend installed by installQml,
// so the bridge library links nothing new; without a backend (tests, tools)
// it does nothing.
#pragma once

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <vector>

class QObject;
class QQmlEngine;

namespace veyra::ui {
struct SubtitleLine;
struct SubtitleView;
}

namespace crispplay::subs {

class OverlayBackend {
public:
    virtual ~OverlayBackend() = default;
    // UI thread, every subtitle tick (16 ms): add the translated line, if any,
    // to what the layered subtitle window draws. `client` is that window's size.
    virtual void appendLines(std::vector<veyra::ui::SubtitleLine>& draw, const veyra::ui::SubtitleView& view, const RECT& client) = 0;
};

namespace detail {
inline std::atomic<OverlayBackend*> gOverlay{nullptr};
}
// The backend must live until the process ends.
inline void installOverlay(OverlayBackend* backend) { detail::gOverlay.store(backend, std::memory_order_release); }

inline void appendLines(std::vector<veyra::ui::SubtitleLine>& draw, const veyra::ui::SubtitleView& view, const RECT& client) {
    if (auto* backend = detail::gOverlay.load(std::memory_order_acquire)) backend->appendLines(draw, view, client);
}

// Creates the `crispSubs` context object (custom/qml/SubsSettings.qml,
// SubsHost.qml) and installs the overlay backend. Settings live in
// <dataDirectory>/crispplay-subs.v1.json (default: off). `bridge` is the
// upstream player bridge (read only: playback position for the event log).
void installQml(QQmlEngine& engine, QObject* bridge, const std::filesystem::path& dataDirectory);

} // namespace crispplay::subs
