// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/tools.hpp"
#include <iostream>
#include <stdexcept>
using namespace rich4;
namespace {
void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
World sample(){
    World w;w.players.resize(2);w.nodes.resize(6);w.lands.resize(3);w.facilities.resize(3);w.tool_supply.fill(10);
    for(std::size_t i=1;i<w.nodes.size();++i){auto& n=w.nodes[i];n.id=static_cast<std::uint32_t>(i);n.x=static_cast<std::int16_t>(i*32);n.y=128;n.neighbors={static_cast<std::uint16_t>(i==5?1:i+1),static_cast<std::uint16_t>(i==1?5:i-1),0,0};}
    for(std::size_t i=0;i<w.players.size();++i){auto& p=w.players[i].state;p.slot=i;p.controller_flags=1;p.node_id=static_cast<std::uint16_t>(i+1);p.last_node_id=5;p.x=static_cast<std::uint16_t>(32*(i+1));p.y=128;p.dice_count=1;p.tool_amounts.fill(2);w.nodes[i+1].flags|=0x100U<<i;}
    return w;
}
void transport(){auto w=sample();Random r(3);auto before=w.tool_supply;
    require(tools::use(w,r,0,5).applied,"equip motorcycle");auto& p=w.players[0].state;
    require(p.traffic_method==1 && p.dice_count==2 && p.tool_amounts[4]==1,"motorcycle state");
    require(!tools::use(w,r,0,5).applied && p.tool_amounts[4]==1,"same transport must not consume");
    require(tools::use(w,r,0,6).applied && p.tool_amounts[4]==2 && p.tool_amounts[5]==1 && p.dice_count==3,"return previous vehicle");
    require(tools::use(w,r,0,12).applied && p.traffic_method==31 && p.dice_count==1 && w.players[0].status.flags_100==2 && w.players[0].status.flags_101==3,"construction saves prior transport");
    require(w.tool_supply==before && r.state()==3,"equipment neither returns shared stock nor consumes randomness");
    require(tools::dismount(w,0) && p.dice_count==1 && !p.traffic_method,"dismount");
}
void obstacles(){auto w=sample();Random r(3);
    require(!tools::use(w,r,0,2,1).applied,"occupied road rejected");
    require(tools::use(w,r,0,2,3).applied,"barrier placement");
    require(w.objects[16].kind==16 && w.objects[16].node_id==3 && ((w.nodes[3].flags>>16)&255)==17,"original barrier record slot");
    require(w.players[0].state.tool_amounts[1]==1 && w.tool_supply[1]==10,"placed tool remains reserved");
    require(!tools::use(w,r,0,3,3).applied,"cannot overlap objects");
    require(tools::use(w,r,0,3,4).applied && w.objects[26].kind==17,"mine slots");
    require(tools::use(w,r,0,4,5).applied && w.objects[36].kind==18,"bomb slots");
}
void teleport(){auto w=sample();Random r(3);
    auto& land=w.lands[1];land.economy.owner_code=1;land.economy.level=4;land.economy.chain_store=1;land.expired_date=123;land.removed_building=9;
    require(tools::use(w,r,0,11,2001,2002).applied,"relocate land");
    require(w.lands[2].economy.owner_code==1 && w.lands[2].economy.level==4 && w.lands[2].expired_date==123 && !land.economy.owner_code && !land.removed_building,"property fields transferred");
    unsigned stored=0;tools::Callbacks callbacks;callbacks.before_teleport=[&](std::size_t slot){require(slot==0,"snapshot actor");++stored;};
    w.objects[0].attached_player=1;w.objects[0].node_id=1;
    require(tools::use(w,r,0,11,0x8001,4,callbacks).applied,"teleport player");
    require(stored==1 && w.players[0].state.node_id==4 && w.players[0].state.x==128 && w.objects[0].node_id==4,"actor and attached object move");
    require(!(w.nodes[1].flags&0x100) && (w.nodes[4].flags&0x100),"occupancy follows player");
}
void callbacks(){auto w=sample();Random r(3);tools::Callbacks callbacks;
    auto result=tools::use(w,r,0,7,2001);
    require(!result.supported && w.players[0].state.tool_amounts[6]==2,"missing blast must not consume");
    unsigned blasts=0;callbacks.blast=[&](auto target,bool nuclear,auto slot){require(target==2001 && nuclear && slot==0,"blast arguments");++blasts;};
    require(tools::use(w,r,0,13,2001,0,callbacks).applied && blasts==1,"nuclear dispatch");
    unsigned face=0;callbacks.remote_dice=[&](auto slot,unsigned n){
        require(slot==0,"die actor");
        require(w.players[slot].state.tool_amounts[7] == 2 && w.tool_supply[7] == 10,
                "controlled die begins and snapshots history before its inventory consumption");
        face=n;
    };
    require(!tools::use(w,r,0,8,7,0,callbacks).applied,"invalid controlled die");
    require(tools::use(w,r,0,8,6,0,callbacks).applied && face==6 && w.tool_supply[7]==11,"controlled die returned to supply");
    callbacks.restore_last_state=[](){return false;};
    require(!tools::use(w,r,0,10,0,0,callbacks).applied && w.players[0].state.tool_amounts[9]==2,"failed restore preserves tool");
}
}
int main(){try{transport();obstacles();teleport();callbacks();std::cout<<"equipment, object reservation, teleport and callback transaction tests passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
