// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

namespace AyuNetworkPrivacy {

void SetEnabled(bool enabled);

[[nodiscard]] int TemporaryKeyExpiresIn();
[[nodiscard]] bool StepUpTemporaryKeyExpiresIn();

} // namespace AyuNetworkPrivacy
