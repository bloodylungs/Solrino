#pragma once

namespace chatterino::solrino {

/// True in a second copy of Solrino opened while another one is already
/// running. Things that act on their own (raid shoutouts, auto-redeem, opening
/// the browser when someone goes live) only happen in the first copy, so they
/// never happen twice.
inline bool &secondInstanceFlag()
{
    static bool second = false;
    return second;
}

inline bool isSecondInstance()
{
    return secondInstanceFlag();
}

}  // namespace chatterino::solrino
