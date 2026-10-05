// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/cards.hpp"
#include <bit>
#include <iostream>
#include <stdexcept>

namespace {
using namespace rich4;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
World fixture(){
    World w;w.header.price_index=1;w.players.resize(4);w.nodes.resize(12);w.lands.resize(5);w.facilities.resize(3);w.commercials.resize(3);
    for(std::size_t i=0;i<4;++i){auto& p=w.players[i];p.state.slot=i;p.state.x=100+unsigned(i)*20;p.state.y=100;p.state.node_id=std::uint16_t(i+1);p.state.cash=100000;p.state.bank_balance=10000;p.state.controller_flags=2;p.state.dice_count=1;p.status.card_strategy=1;p.status.rescue_strategy=2;}
    for(std::size_t i=1;i<w.lands.size();++i){auto& p=w.lands[i];p.land_price=1000;p.price_per_level=500;p.economy.street_name=i==4?"other":"road";p.economy.level=1;p.economy.toll_by_level={10,100,1000,3000,10000,20000};w.nodes[i].type_and_index=std::uint16_t(2000+i);}
    for(std::size_t i=1;i<w.facilities.size();++i){auto& p=w.facilities[i];p.land_price=2000;p.price_per_level()=1000;p.facility_type=1;p.level=3;p.owner_code=2;w.nodes[4+i].type_and_index=std::uint16_t(4000+i);}
    for(auto& s:w.stocks){s.float_bits[1]=std::bit_cast<std::uint32_t>(100.F);s.float_bits[2]=std::bit_cast<std::uint32_t>(100.F);}
    return w;
}
CardCallbacks callbacks(){CardCallbacks cb;cb.market_closed=[] {return false;};cb.nearby_instances=[](auto){return std::vector<std::uint16_t>{2001,2002,2003,2004,4001,4002,0x8001,0x8002,0x8004,0x8008};};return cb;}
std::optional<CardUse> choose(World& w,std::uint8_t id,Random& r,const CardCallbacks& cb=callbacks(),CardAiMemory* memory=nullptr){w.players[0].state.cards={id};return choose_ai_card(w,r,0,cb,memory);}
bool actor_target(const std::optional<CardUse>& use,unsigned slot){return use && use->target && use->target->kind==CardTargetKind::actor && use->target->value==slot;}
bool property_target(const std::optional<CardUse>& use,unsigned code){return use && use->target && use->target->kind==CardTargetKind::property && use->target->value==code;}
void visit_order_and_random(){
    auto w=fixture();Random r(43);w.players[0].status.card_strategy=0;require(!choose(w,15,r) && r.state()==43,"disabled AI consumes no RNG");
    w.players[0].status.card_strategy=1;w.players[0].status.rescue_strategy=0;require(!choose(w,15,r) && r.state()==43,"aggression difference two skips without RNG");
    w.players[0].status.rescue_strategy=1;Random expected(43);const bool allowed=expected.next()%3==0;const bool winter=allowed && expected.next()%4==0;
    require(bool(choose(w,15,r))==winter && r.state()==expected.state(),"gate and winter predicate draw order");
    w.players[0].status.rescue_strategy=2;w.players[0].state.cards={18,19,20,21,5,6,18,19,20};r.seed(99);expected.seed(99);(void)expected.next();
    require(!choose_ai_card(w,r,0,callbacks()) && r.state()==expected.state(),"large passive hand consumes only randomized scan start");
    w.players[0].state.cards={22,15};w.players[0].status.god_info=1;w.objects[0].kind=5;r.seed(99);
    const auto use=choose_ai_card(w,r,0,callbacks());require(use && use->card_id==22 && r.state()==99,"first qualifying card ends visit before later predicate RNG");
}
void economic_predicates(){
    auto w=fixture();Random r(2);w.players[0].state.cash=100;
    require(choose(w,1,r).has_value() && r.state()==2,"uniform wealth threshold");
    w.players[0].status.hostility[1]=10;require(actor_target(choose(w,2,r),1),"equalizer first favors richest qualifying enemy");
    w.players[0].status.hostility={};require(actor_target(choose(w,2,r),3),"equalizer fallback overwrites with last qualifying slot");
    w=fixture();w.lands[1].economy.owner_code=2;w.lands[1].economy.level=2;w.players[0].status.hostility[1]=1;
    require(choose(w,3,r).has_value(),"purchase enemy level two");w.players[0].state.cash=2000;require(!choose(w,3,r),"computer purchase excludes exact cash equality");
    w=fixture();w.lands[1].economy.owner_code=1;w.lands[4].economy.owner_code=2;w.lands[4].economy.level=3;w.lands[4].land_price=2000;w.players[0].status.hostility[1]=1;
    require(property_target(choose(w,4,r),2004),"exchange identifies pricier higher enemy property on another street");
    require(!choose(w,5,r) && !choose(w,6,r),"original computer never initiates exchange-house or turn-around");
    w.players[0].status.rescue_strategy=0;require(choose(w,7,r).has_value(),"profile zero converts own single building");
    w=fixture();w.nodes[1].type_and_index=4001;w.facilities[1].owner_code=1;w.facilities[1].facility_type=0;w.facilities[1].level=1;r.seed(7);Random expected(7);const auto type=expected.next()%4+1;
    const auto reconstruction=choose(w,7,r);require(reconstruction && reconstruction->choice->value==type && r.state()==expected.state(),"facility reconstruction draws one exact type");
    w.facilities[1].owner_code=2;w.facilities[1].level=3;require(choose(w,8,r).has_value(),"auction targets opponent level three");
    w=fixture();w.players[1].state.cash=30001;w.players[2].state.cash=50001;w.players[3].state.cash=50001;w.players[0].status.hostility[1]=1;
    require(actor_target(choose(w,26,r),1),"tax lower threshold for enemy");w.players[1].state.cash=30000;require(actor_target(choose(w,26,r),3),"tax fallback last qualifying player");
}
void construction_and_inventory_predicates(){
    auto w=fixture();Random r(5);for(unsigned i=1;i<=3;++i)w.lands[i].economy.owner_code=1;Random expected(5);(void)expected.next();
    require(property_target(choose(w,9,r),2001) && r.state()==expected.state(),"angel random choice even with one eligible street");
    w=fixture();w.players[0].status.hostility[1]=1;w.lands[1].economy.owner_code=2;w.lands[1].economy.level=3;w.lands[2].economy.owner_code=2;w.lands[2].economy.level=4;
    require(property_target(choose(w,10,r),2001),"devil enemy street requires two buildings and seven levels");
    require(property_target(choose(w,11,r),4001),"monster prefers hostile player's facility");
    w.facilities[1].level=0;w.facilities[2].level=0;require(property_target(choose(w,11,r),2002),"monster enemy highest land level");
    w=fixture();w.facilities[1].facility_type=3;w.facilities[1].level=1;w.facilities[2].level=0;w.players[0].state.traffic_method=2;
    require(property_target(choose(w,12,r),4001),"demolition gas station transport predicate");
    w=fixture();w.players[0].status.hostility[1]=1;w.players[1].state.cards={15,1,2};
    auto robbery=choose(w,13,r);require(actor_target(robbery,1) && robbery->choice->value==1,"robbery chooses enemy first maximum-price aggressive card");
    w.players[0].status.hostility={};w.players[1].state.cards={26};w.players[2].state.cards={15};w.players[3].state.cards={2};
    robbery=choose(w,13,r);require(actor_target(robbery,3) && robbery->choice->value==2,"robbery fallback aggression exactly two");
}
void status_and_god_predicates(){
    auto w=fixture();Random r(19);w.lands[1].economy.owner_code=1;w.lands[2].economy.owner_code=1;
    require(actor_target(choose(w,14,r),0),"stop builds own connected street");
    w.lands[2].economy.owner_code=0;w.lands[1].economy.level=2;CardAiMemory memory;memory.parameter0=0x8004;
    require(actor_target(choose(w,14,r,callbacks(),&memory),2),"isolated stop predicate preserves original stale target parameter");
    w=fixture();w.players[0].status.hostility[2]=100;w.players[2].state.cards={18};w.players[1].status.days_sleeping=1;r.seed(19);Random expected(19);(void)expected.next();
    require(actor_target(choose(w,16,r),3) && r.state()==expected.state(),"sleepwalk excludes sleep and revenge; fallback draws despite singleton");
    w.players[2].state.cards={};r.seed(19);require(actor_target(choose(w,17,r),2) && r.state()==19,"prison enemy target consumes no selection random");
    w=fixture();w.players[0].status.attached_bomb=20;w.objects[19].remaining_days=12;
    require(choose(w,22,r).has_value(),"send-god bomb threshold below thirteen");w.players[0].status.god_info=2;w.objects[1].kind=1;
    require(!choose(w,22,r),"attached benign god prevents computer inspecting bomb branch");
    w.players[0].status.god_info=0;w.objects[0].kind=1;w.objects[0].node_id=9;w.nodes[9].x=120;w.nodes[9].y=100;
    auto cb=callbacks();cb.nearby_instances=[](auto){return std::vector<std::uint16_t>{0x8100};};
    require(choose(w,23,r,cb).has_value(),"summon nearest object ID one");w.players[0].status.god_info=12;
    require(!choose(w,23,r,cb),"computer avoids replacing friendly attached object ID");
}
void stock_street_alliance_and_routes(){
    auto w=fixture();Random r(1);w.players[0].stocks[2]={100,std::bit_cast<std::uint32_t>(20.F)};
    auto use=choose(w,24,r);require(use && use->target==CardTarget{CardTargetKind::stock,2},"red chooses highest holding cost");
    w.stocks[2].suspension_days=1;require(!choose(w,24,r),"computer red excludes suspension despite manual allowance");
    w=fixture();w.players[0].status.hostility[1]=1;w.players[1].stocks[3]={50,std::bit_cast<std::uint32_t>(30.F)};
    use=choose(w,25,r);require(use && use->target==CardTarget{CardTargetKind::stock,3},"black targets enemy holding absent from own portfolio");
    w.players[0].stocks[3].amount=1;require(!choose(w,25,r),"black avoids own stock");
    w=fixture();w.lands[1].economy.owner_code=1;w.lands[1].economy.level=4;w.lands[2].economy.owner_code=1;w.lands[2].economy.level=3;
    require(property_target(choose(w,27,r),2001),"price rise needs two thirds street and seven own levels");
    w=fixture();w.players[0].status.hostility[1]=1;w.lands[1].economy.owner_code=2;w.lands[1].economy.level=4;w.lands[2].economy.owner_code=2;w.lands[2].economy.level=3;
    auto cb=callbacks();unsigned calls=0;cb.lookahead_nodes=[&](auto actor,auto count){require(actor==0 && count==6,"seal lookahead arguments");++calls;return CardLookahead{{1,2,3,4,5,6},false};};
    require(property_target(choose(w,28,r,cb),2001) && calls==1,"seal evaluates six-step route");
    w=fixture();w.lands[1].economy.owner_code=3;w.lands[2].economy.owner_code=3;w.lands[3].economy.owner_code=3;
    require(actor_target(choose(w,29,r),2),"alliance selects global land-count leader when eligible nearby");w.players[0].status.hostility[2]=1;require(!choose(w,29,r),"alliance excludes most hostile leader");
    w=fixture();cb=callbacks();cb.lookahead_nodes=[](auto actor,auto count){require(actor==0 && count==3,"tortoise first predicts own three steps");return CardLookahead{{1,2,3},false};};
    require(actor_target(choose(w,30,r,cb),0),"tortoise self three cheap acquisition opportunities");
    w=fixture();for(unsigned i=1;i<=3;++i){w.lands[i].economy.owner_code=1;w.lands[i].economy.level=4;}cb=callbacks();calls=0;
    cb.lookahead_nodes=[&](auto actor,auto count){require(count==3,"all tortoise paths length three");++calls;return CardLookahead{{1,2,3},actor==0};};
    require(actor_target(choose(w,30,r,cb),1) && calls==2,"tortoise rejects own junction path then traps first qualifying opponent");
}
} // namespace
int main(){try{visit_order_and_random();economic_predicates();construction_and_inventory_predicates();status_and_god_predicates();stock_street_alliance_and_routes();std::cout<<"card AI tests passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
