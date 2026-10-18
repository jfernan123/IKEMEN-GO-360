#pragma once

namespace m360 {

// The build's version, shown in a corner of the fight so a console's build can be told apart: "V<stamp> <commit>"
// for a published release (the stamp is in the release's title), "DEV <date> <commit>" otherwise. Written by
// cmake/build_version.cmake at build time.
const char *buildVersion();

} // namespace m360
