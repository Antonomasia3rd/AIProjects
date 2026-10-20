#pragma once
#include "tile_delivery_policy.h"
namespace aip { namespace content {
inline thread_local const TileDeliveryPolicy* g_currentTileDeliveryPolicy=nullptr;
inline const TileDeliveryPolicy* CurrentTileDeliveryPolicy(){return g_currentTileDeliveryPolicy;}
inline bool TryReadTilePolicy(const std::wstring& section,const std::wstring& key,std::wstring& value){
    return g_currentTileDeliveryPolicy&&g_currentTileDeliveryPolicy->Lookup(section,key,value);
}
class TilePolicyReadScope {
    const TileDeliveryPolicy* previous_;
public:
    explicit TilePolicyReadScope(const TileDeliveryPolicy& policy):previous_(g_currentTileDeliveryPolicy){
        g_currentTileDeliveryPolicy=policy.included?&policy:nullptr;
    }
    ~TilePolicyReadScope(){g_currentTileDeliveryPolicy=previous_;}
    TilePolicyReadScope(const TilePolicyReadScope&)=delete;
    TilePolicyReadScope& operator=(const TilePolicyReadScope&)=delete;
};
} }
