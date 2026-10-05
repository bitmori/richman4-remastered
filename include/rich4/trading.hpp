// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rich4/world.hpp"

#include <optional>

namespace rich4 {
class Random;
namespace trading {

struct Item {
    OfferKind kind = OfferKind::empty;
    std::uint16_t id = 0;
    std::uint16_t maximum_quantity = 1;
};

struct ListingLimits {
    std::int32_t reference_price = 0;
    std::int32_t maximum_price = 0;
};

struct Listing {
    OfferKind kind = OfferKind::empty;
    std::uint16_t item = 0;
    std::int32_t price = 0;
    std::uint16_t quantity = 0; // Required for stock only.
};

struct Quote {
    std::size_t seller = 0, index = 0;
    TradeOffer offer{};
};

struct Result {
    bool applied = false;
    std::string message;
    bool commercial_owner_changed = false;
};

struct AiResult {
    std::size_t removed = 0, posted = 0, repriced = 0;
    std::optional<Quote> purchased;
};

// Original sale UI allows direct offers on every calendar day. Posting reserves
// no inventory; ownership, building state and availability are checked later.
[[nodiscard]] std::vector<Item> listable_items(const World &, std::size_t seller);
[[nodiscard]] std::optional<ListingLimits> listing_limits(const World &, std::size_t seller, OfferKind kind,
                                                          std::uint16_t item,
                                                          std::uint16_t stock_quantity = 0);
[[nodiscard]] std::vector<Quote> quotes(const World &);
Result post(World &, std::size_t seller, const Listing &);
bool cancel(World &, std::size_t seller, std::size_t index);
// Original 0x42483e, called when entering the market and before AI sale actions.
std::size_t prune(World &);
// Original 0x4255da plus caller's successful-offer removal. Buyer pays CASH;
// seller receives BANK. Direct share transfers do not move the market price.
Result buy(World &, std::size_t buyer, std::size_t seller, std::size_t index);
// Original 0x42886e: cleanup, 1/15 posting, 1/3 repricing, 1/4 bargain purchase.
// Caller performs original 0x436b0a(mode0) financing reconciliation afterward.
AiResult ai_turn(World &, Random &, std::size_t actor);

} // namespace trading
} // namespace rich4
