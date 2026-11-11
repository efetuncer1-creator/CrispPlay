// Instant replay: interface side. One call from apps/veyra-qml/main.cpp
// (VEYRA_CUSTOM hook) creates the `crispReplay` context object that
// custom/qml/ReplayHost.qml and ReplaySettings.qml talk to.
#pragma once

#include <filesystem>

class QQmlEngine;

namespace crispplay::replay {

// Settings live in <dataDirectory>/crispplay-replay.v1.json (default: off).
void installQml(QQmlEngine& engine, const std::filesystem::path& dataDirectory);

} // namespace crispplay::replay
