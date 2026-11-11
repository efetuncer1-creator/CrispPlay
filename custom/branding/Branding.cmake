# CrispPlay branding, included from the root CMakeLists.txt right after project()
# through a single VEYRA_CUSTOM hook line. Keep everything brand-specific here.

# Our patch level on top of the upstream release in project(VERSION).
# Bump it for each CrispPlay release; reset to 1 after taking a new upstream tag.
set(CRISPPLAY_PATCH 1 CACHE STRING "CrispPlay patch level (-crispplay.N)")

set(CRISPPLAY_PRODUCT_NAME "CrispPlay")
# ASCII only: the version resource is compiled by rc.exe.
set(CRISPPLAY_FILE_DESCRIPTION "CrispPlay Video Player (based on Veyra)")

# Always derived from the upstream version, so a sync that bumps project(VERSION)
# can never ship a stale label. Upstream's own check right below still passes
# because the label starts with PROJECT_VERSION.
set(VEYRA_DISPLAY_VERSION "${PROJECT_VERSION}-crispplay.${CRISPPLAY_PATCH}"
    CACHE STRING "Human-readable version string" FORCE)

# Header-only branding/policy headers (crispplay/*.h) for every target.
include_directories("${CMAKE_CURRENT_LIST_DIR}/../include")
