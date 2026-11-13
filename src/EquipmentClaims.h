#pragma once
#include <cstdint>
namespace tds {
constexpr std::uint32_t equipmentClaimRecord=0x314D4C43; // CLM1, independent of progression/Hardcore
constexpr std::uint32_t equipmentClaimAll=15;
constexpr bool reserveEquipmentClaim(std::uint32_t& mask,unsigned group) {
 if(group>=4 || (mask & (1u<<group))) return false;
 mask|=1u<<group; return true;
}
constexpr std::uint32_t decodeEquipmentClaim(std::uint32_t version,std::uint32_t length,std::uint32_t read,std::uint32_t value,bool duplicate) {
 return !duplicate && version==1 && length==sizeof(std::uint32_t) && read==sizeof(std::uint32_t) && (value & ~equipmentClaimAll)==0 ? value : equipmentClaimAll;
}
}
