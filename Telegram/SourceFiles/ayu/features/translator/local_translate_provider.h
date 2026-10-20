// This is the source code of freshGram.
#pragma once

#include "lang/translate_provider.h"

namespace Ayu::Translator {

[[nodiscard]] std::unique_ptr<Ui::TranslateProvider> CreateLocalTranslateProvider();

} // namespace Ayu::Translator
