#pragma once
namespace dsloader::proxy {
/// Loads %SystemRoot%\System32\version.dll and resolves the forwarded exports.
bool loadReal();
void unloadReal();
}  // namespace dsloader::proxy
