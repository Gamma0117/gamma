#include "entity/player_messages.h"

namespace aurora::entity {

std::string_view placeResultName(PlaceResult result)
{
    switch (result) {
    case PlaceResult::None:
        return "none";
    case PlaceResult::Applied:
        return "placed";
    case PlaceResult::NoTarget:
        return "no target";
    case PlaceResult::InvalidSlot:
        return "invalid slot";
    case PlaceResult::Height:
        return "outside the editable height";
    case PlaceResult::Occupied:
        return "occupied";
    case PlaceResult::Unloaded:
        return "not loaded";
    case PlaceResult::BlocksPlayer:
        return "blocks the player";
    }
    return "?";
}

} // namespace aurora::entity
