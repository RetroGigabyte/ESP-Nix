#pragma once

// Single source of truth for the OS version string, shared by the shell
// itself, Retron (via the "version" pseudo-variable), and compiled
// modules loaded through runmod (via host_os_version()) - previously
// this string was duplicated across half a dozen places in commands.h/
// shell.h/README.md, all bumped by hand on every release.
#define ESP_NIX_VERSION "1.3.5"
