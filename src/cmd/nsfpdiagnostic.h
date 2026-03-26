// nsfpdiagnostic -- Diagnostic utility for dumping NSFileProvider domain information.
// Invoked via the --dump-nsfp-domains CLI flag on macOS.
// 2026-03-07: Initial creation (VOD-027).
#pragma once

#ifdef Q_OS_MACOS

namespace OCC {

/// Queries all registered NSFileProvider domains and prints their details
/// (identifier, display name, path) to stdout. Blocks the calling thread
/// until the query completes via a semaphore. Returns 0 on success, 1 on error.
int dumpNSFileProviderDomains();

} // namespace OCC

#endif // Q_OS_MACOS
