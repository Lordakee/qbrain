#pragma once

// N37 D2: C++ source of truth for the Qbrain release version. The build-level
// single source of truth is project(qbrain VERSION ...) in CMakeLists.txt;
// this header must carry the same value (the n37_packaging unit asserts the
// constants, and the hard audit asserts consistency with CMakeLists.txt).
// Global scope (not namespace qbrain) on purpose: tests and the CLI wiring
// consume the constants unqualified.
inline constexpr int QBRAIN_VERSION_MAJOR = 2;
inline constexpr int QBRAIN_VERSION_MINOR = 0;
inline constexpr int QBRAIN_VERSION_PATCH = 0;

// "MAJOR.MINOR.PATCH" rendered from the constants above.
inline constexpr const char* QBRAIN_VERSION_STRING = "2.0.0";
