#pragma once

#include <QString>

namespace chatterino {

/// Buying a Solrino badge for someone else, like a gifted sub. Only badges
/// that are for sale can be gifted. Until checkout opens, the last step says
/// it's coming soon.
void openSolrinoGiftDialog(const QString &forLogin = {});

}  // namespace chatterino
