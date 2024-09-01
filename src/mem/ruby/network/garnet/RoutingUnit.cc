/*
 * Copyright (c) 2008 Princeton University
 * Copyright (c) 2016 Georgia Institute of Technology
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include "mem/ruby/network/garnet/RoutingUnit.hh"

#include "base/cast.hh"
#include "base/compiler.hh"
#include "debug/RubyNetwork.hh"
#include "debug/RubyResourceStalls.hh"
#include "mem/ruby/network/garnet/InputUnit.hh"
#include "mem/ruby/network/garnet/OutputUnit.hh"
#include "mem/ruby/network/garnet/Router.hh"
#include "mem/ruby/slicc_interface/Message.hh"

namespace gem5
{

namespace ruby
{

namespace garnet
{

RoutingUnit::RoutingUnit(Router *router)
{
    m_router = router;
    m_routing_table.clear();
    m_weight_table.clear();
}

void
RoutingUnit::addRoute(std::vector<NetDest>& routing_table_entry)
{
    if (routing_table_entry.size() > m_routing_table.size()) {
        m_routing_table.resize(routing_table_entry.size());
    }
    for (int v = 0; v < routing_table_entry.size(); v++) {
        m_routing_table[v].push_back(routing_table_entry[v]);
    }
}

void
RoutingUnit::addWeight(int link_weight)
{
    m_weight_table.push_back(link_weight);
}

bool
RoutingUnit::supportsVnet(int vnet, std::vector<int> sVnets)
{
    // If all vnets are supported, return true
    if (sVnets.size() == 0) {
        return true;
    }

    // Find the vnet in the vector, return true
    if (std::find(sVnets.begin(), sVnets.end(), vnet) != sVnets.end()) {
        return true;
    }

    // Not supported vnet
    return false;
}

/*
 * This is the default routing algorithm in garnet.
 * The routing table is populated during topology creation.
 * Routes can be biased via weight assignments in the topology file.
 * Correct weight assignments are critical to provide deadlock avoidance.
 */
int
RoutingUnit::lookupRoutingTable(int vnet, NetDest msg_destination)
{
    // First find all possible output link candidates
    // For ordered vnet, just choose the first
    // (to make sure different packets don't choose different routes)
    // For unordered vnet, randomly choose any of the links
    // To have a strict ordering between links, they should be given
    // different weights in the topology file

    int output_link = -1;
    int min_weight = INFINITE_;
    std::vector<int> output_link_candidates;
    int num_candidates = 0;

    // Identify the minimum weight among the candidate output links
    for (int link = 0; link < m_routing_table[vnet].size(); link++) {
        if (msg_destination.intersectionIsNotEmpty(
            m_routing_table[vnet][link])) {

        if (m_weight_table[link] <= min_weight)
            min_weight = m_weight_table[link];
        }
    }

    // Collect all candidate output links with this minimum weight
    for (int link = 0; link < m_routing_table[vnet].size(); link++) {
        if (msg_destination.intersectionIsNotEmpty(
            m_routing_table[vnet][link])) {

            if (m_weight_table[link] == min_weight) {
                num_candidates++;
                output_link_candidates.push_back(link);
            }
        }
    }

    if (output_link_candidates.size() == 0) {
        fatal("Fatal Error:: No Route exists from this Router.");
        exit(0);
    }

    // Randomly select any candidate output link
    int candidate = 0;
    if (!(m_router->get_net_ptr())->isVNetOrdered(vnet))
        candidate = rand() % num_candidates;

    output_link = output_link_candidates.at(candidate);
    return output_link;
}


void
RoutingUnit::addInDirection(PortDirection inport_dirn, int inport_idx)
{
    m_inports_dirn2idx[inport_dirn] = inport_idx;
    m_inports_idx2dirn[inport_idx]  = inport_dirn;
}

void
RoutingUnit::addOutDirection(PortDirection outport_dirn, int outport_idx)
{
    m_outports_dirn2idx[outport_dirn] = outport_idx;
    m_outports_idx2dirn[outport_idx]  = outport_dirn;
}

// outportCompute() is called by the InputUnit
// It calls the routing table by default.
// A template for adaptive topology-specific routing algorithm
// implementations using port directions rather than a static routing
// table is provided here.

int
RoutingUnit::outportCompute(RouteInfo route, int inport,
                            PortDirection inport_dirn)
{
    int outport = -1;

    if (route.dest_router == m_router->get_id()) {

        // Multiple NIs may be connected to this router,
        // all with output port direction = "Local"
        // Get exact outport id from table
        outport = lookupRoutingTable(route.vnet, route.net_dest);
        return outport;
    }

    // Routing Algorithm set in GarnetNetwork.py
    // Can be over-ridden from command line using --routing-algorithm = 1
    RoutingAlgorithm routing_algorithm =
        (RoutingAlgorithm) m_router->get_net_ptr()->getRoutingAlgorithm();

    assert(routing_algorithm != CUSTOM_ && routing_algorithm != DOR_ && routing_algorithm != RANDOM_DIMENSION_);
    switch (routing_algorithm) {
        case TABLE_:  outport =
            lookupRoutingTable(route.vnet, route.net_dest); break;
        case XY_:     outport =
            outportComputeXY(route, inport, inport_dirn); break;
        // any custom algorithm
        case CUSTOM_: outport =
            outportComputeCustom(route, inport, inport_dirn); break;
        default: outport =
            lookupRoutingTable(route.vnet, route.net_dest); break;
    }

    assert(outport != -1);
    return outport;
}

std::pair<int, Dor_type>
RoutingUnit::outportCompute_dor(RouteInfo route, int inport,
                            PortDirection inport_dirn, int invc)
{
    int outport = -1;
    Dor_type Dor;

    if (route.dest_router == m_router->get_id()) {

        // Multiple NIs may be connected to this router,
        // all with output port direction = "Local"
        // Get exact outport id from table
        outport = lookupRoutingTable(route.vnet, route.net_dest);
        return std::make_pair(outport, DEVICE_);
    }

    // Routing Algorithm set in GarnetNetwork.py
    // Can be over-ridden from command line using --routing-algorithm = 1
    RoutingAlgorithm routing_algorithm =
        (RoutingAlgorithm) m_router->get_net_ptr()->getRoutingAlgorithm();

    assert(routing_algorithm == CUSTOM_ || routing_algorithm == DOR_ || routing_algorithm == RANDOM_DIMENSION_);

    switch (routing_algorithm) {
        case TABLE_:  outport =
            lookupRoutingTable(route.vnet, route.net_dest); break;
        case XY_:     outport =
            outportComputeXY(route, inport, inport_dirn); break;
        // any custom algorithm
        case CUSTOM_: std::tie(outport, Dor) =
            outportComputeCustom_dor(route, inport, inport_dirn, invc); break;
        case DOR_: std::tie(outport, Dor) =
            outportComputeCustom_trivial_dor(route, inport, inport_dirn, invc); break;
        case RANDOM_DIMENSION_: std::tie(outport, Dor) =
            outportComputeCustom_random_dimension(route, inport, inport_dirn, invc); break;
        default: outport =
            lookupRoutingTable(route.vnet, route.net_dest); break;
    }

    assert(outport != -1);
    return std::make_pair(outport, Dor);
}

std::pair<int, std::pair<Star_type, bool > >
RoutingUnit::outportCompute_star(RouteInfo route, int inport,
                            PortDirection inport_dirn, int invc)
{
    int outport = -1;
    Star_type Star;

    if (route.dest_router == m_router->get_id()) {

        // Multiple NIs may be connected to this router,
        // all with output port direction = "Local"
        // Get exact outport id from table
        outport = lookupRoutingTable(route.vnet, route.net_dest);
        return std::make_pair(outport, std::make_pair(STARDEVICE_,false));
    }

    // Routing Algorithm set in GarnetNetwork.py
    // Can be over-ridden from command line using --routing-algorithm = 1
    RoutingAlgorithm routing_algorithm =
        (RoutingAlgorithm) m_router->get_net_ptr()->getRoutingAlgorithm();

    assert(routing_algorithm == STAR_CHANNEL_);
    std::pair<int, std::pair<Star_type, bool > > result = outportComputeStarChannel_star(route, inport, inport_dirn, invc);
    assert(result.first != -1);//outport
    return result;
}

// XY routing implemented using port directions
// Only for reference purpose in a Mesh
// By default Garnet uses the routing table
int
RoutingUnit::outportComputeXY(RouteInfo route,
                              int inport,
                              PortDirection inport_dirn)
{
    PortDirection outport_dirn = "Unknown";

    [[maybe_unused]] int num_rows = m_router->get_net_ptr()->getNumRows();
    int num_cols = m_router->get_net_ptr()->getNumCols();
    assert(num_rows > 0 && num_cols > 0);

    int my_id = m_router->get_id();
    int my_x = my_id % num_cols;
    int my_y = my_id / num_cols;

    int dest_id = route.dest_router;
    int dest_x = dest_id % num_cols;
    int dest_y = dest_id / num_cols;

    int x_hops = abs(dest_x - my_x);
    int y_hops = abs(dest_y - my_y);

    bool x_dirn = (dest_x >= my_x);
    bool y_dirn = (dest_y >= my_y);

    // already checked that in outportCompute() function
    assert(!(x_hops == 0 && y_hops == 0));

    if (x_hops > 0) {
        if (x_dirn) {
            assert(inport_dirn == "Local" || inport_dirn == "West");
            outport_dirn = "East";
        } else {
            assert(inport_dirn == "Local" || inport_dirn == "East");
            outport_dirn = "West";
        }
    } else if (y_hops > 0) {
        if (y_dirn) {
            // "Local" or "South" or "West" or "East"
            assert(inport_dirn != "North");
            outport_dirn = "North";
        } else {
            // "Local" or "North" or "West" or "East"
            assert(inport_dirn != "South");
            outport_dirn = "South";
        }
    } else {
        // x_hops == 0 and y_hops == 0
        // this is not possible
        // already checked that in outportCompute() function
        panic("x_hops == y_hops == 0");
    }

    return m_outports_dirn2idx[outport_dirn];
}

// Template for implementing custom routing algorithm
// using port directions. (Example adaptive)
int
RoutingUnit::outportComputeCustom(RouteInfo route,
                                 int inport,
                                 PortDirection inport_dirn)
{

    int my_id = m_router->get_id();
    int dest_id = route.dest_router;

    int difference = dest_id ^ my_id;
    int diffence_bit = difference & -difference;

    int outport = 0;
    while ((diffence_bit & 1) == 0) {
        diffence_bit >>= 1;
        outport++;
    }

    PortDirection outport_dirn = std::to_string(outport);
    return m_outports_dirn2idx[outport_dirn];
}

std::pair<int, Dor_type>
RoutingUnit::outportComputeCustom_dor(RouteInfo route,
                                 int inport,
                                 PortDirection inport_dirn, int invc)
{

    int my_id = m_router->get_id();
    int dest_id = route.dest_router;
    // int length = log2(m_router->get_net_ptr()->getNumRouters());

    int difference = dest_id ^ my_id;
    int dor_position = 31 - __builtin_clz(difference);

    int dor = 1 << dor_position;

    bool is_dest_greater = (dest_id & dor) != 0;

    PortDirection dorport_dirn = "msb " + std::to_string(dor_position);
    int dor_port = m_outports_dirn2idx[dorport_dirn];

    int outport;
    Dor_type Dor;

    auto switch_allocator = m_router->getSwitchAllocator(); //What's this for?
    std::vector<int> out_ports;

    for (int i = 0; i < dor_position; i++) {

            //the ith bit must be different of source and destination
            if ((difference & (1 << i)) == 0) {
                continue;
            }

            outport = m_outports_dirn2idx["msb " + std::to_string(i)];
            //print the key and value of map of m_outports_dirn2idx
            // for (auto it = m_outports_dirn2idx.begin(); it != m_outports_dirn2idx.end(); ++it) {
            //     DPRINTF(RubyResourceStalls, "Router %d Listmap: outport %s %d\n", my_id, it->first, it->second);
            // }

            DPRINTF(RubyResourceStalls, "Insert port to list for use %d corresponding bit %d\n", outport, i);
            Dor = COMMON_;
            if (switch_allocator->send_allowed_dor(inport, invc, outport, -1, Dor) && (outport != 0) && !get_outport_used(outport)){
                out_ports.push_back(outport);
            }
    }

    if (out_ports.size() != 0) {
        //randomly choose one of the available ports
        outport = out_ports[rand() % out_ports.size()];
        Dor = COMMON_;

        DPRINTF(RubyResourceStalls, "Use adaptive routing: Router %d: outport %d\n", my_id, outport);

        set_outport_used(outport, true);
        return std::make_pair(outport, Dor);
    }

    if (is_dest_greater) {
        Dor = STARPLUS_;
    } else {
        Dor = STARMINUS_;
    }


    DPRINTF(RubyResourceStalls, "Use Dor port:%d outport_dirn %s\n", dor_port, dorport_dirn);

    return std::make_pair(dor_port, Dor);
}

std::pair<int, Dor_type>
RoutingUnit::outportComputeCustom_random_dimension(RouteInfo route,
                                 int inport,
                                 PortDirection inport_dirn, int invc)
{

    int my_id = m_router->get_id();
    int dest_id = route.dest_router;
    // int length = log2(m_router->get_net_ptr()->getNumRouters());

    int difference = dest_id ^ my_id;
    int dor_position = 31 - __builtin_clz(difference);

    int dor = 1 << dor_position;

    bool is_dest_greater = (dest_id & dor) != 0;

    PortDirection dorport_dirn = "msb " + std::to_string(dor_position);
    int dor_port = m_outports_dirn2idx[dorport_dirn];

    int outport;
    Dor_type Dor;

    auto switch_allocator = m_router->getSwitchAllocator();
    std::vector<int> out_ports;
    std::vector<int> vacant_out_ports;

    for (int i = 0; i <= dor_position; i++) {

            //the ith bit must be different of source and destination
            if ((difference & (1 << i)) == 0) {
                continue;
            }

            outport = m_outports_dirn2idx["msb " + std::to_string(i)];

            DPRINTF(RubyResourceStalls, "Insert port to list for use %d corresponding bit %d\n", outport, i);
            Dor = COMMON_;
            if (switch_allocator->send_allowed_dor(inport, invc, outport, -1, Dor) && (outport != 0) && !get_outport_used(outport)){ // 不判断是否用了
                vacant_out_ports.push_back(outport);
            }
            out_ports.push_back(outport);
    }
    assert(out_ports.size() != 0);
    if (vacant_out_ports.size() != 0) {
        //randomly choose one of the available ports
        outport = vacant_out_ports[rand() % vacant_out_ports.size()];
        Dor = COMMON_;

        DPRINTF(RubyResourceStalls, "Use adaptive routing: Router %d: outport %d\n", my_id, outport);
        set_outport_used(outport, true);
        return std::make_pair(outport, Dor);
    }
    else{
        outport = out_ports[rand() % out_ports.size()];
        Dor = COMMON_;
        DPRINTF(RubyResourceStalls, "Use adaptive routing: Router %d: outport %d\n", my_id, outport);
        set_outport_used(outport, true);
        return std::make_pair(outport, Dor);
    }
}

std::pair<int, Dor_type>
RoutingUnit::outportComputeCustom_trivial_dor(RouteInfo route,
                                 int inport,
                                 PortDirection inport_dirn, int invc)
{


    int my_id = m_router->get_id();
    int dest_id = route.dest_router;
    // int length = log2(m_router->get_net_ptr()->getNumRouters());

    int difference = dest_id ^ my_id;
    int dor_position = 31 - __builtin_clz(difference);

    int dor = 1 << dor_position;

    bool is_dest_greater = (dest_id & dor) != 0;

    PortDirection dorport_dirn = "msb " + std::to_string(dor_position);
    int dor_port = m_outports_dirn2idx[dorport_dirn];

    // int outport;
    Dor_type Dor;

    if (is_dest_greater) {
        Dor = STARPLUS_;
    } else {
        Dor = STARMINUS_;
    }

    DPRINTF(RubyResourceStalls, "Use Dor port:%d outport_dirn %s\n", dor_port, dorport_dirn);

    return std::make_pair(dor_port, Dor);
}

std::pair<int, std::pair<Star_type,bool > >// outport, Star, is_wrap //need to modify others
RoutingUnit::outportComputeStarChannel_star(RouteInfo route,
                                 int inport,
                                 PortDirection inport_dirn, int invc)
{
    int my_id = m_router->get_id();
    int dest_id = route.dest_router;
    int num_dim = m_router->get_net_ptr()->getNumDim();
    int num_ary = m_router->get_net_ptr()->getNumAry();

    std::vector<int> dir; // minimal direction for dim i. +1: plus. -1: minus; 0: no difference
    std::vector<int> pw;
    std::vector<int> digit_my, digit_dest;
    std::vector<bool> is_wrap; // if it's wrap-around on the i-th dimension 
    pw.push_back(1);for(int i=1;i<num_dim;i++)pw.push_back(pw.back()*num_ary);
    for(int i=0;i<num_dim;i++)digit_my.push_back(my_id/pw[i]%num_ary);
    for(int i=0;i<num_dim;i++)digit_dest.push_back(dest_id/pw[i]%num_ary);
    int most_sig_diff_bit = -1;
    for(int i=0;i<num_dim;i++){
        bool flg_is_wrap = false;
        if(digit_my[i]<digit_dest[i]){
            if(digit_dest[i]-digit_my[i]<=num_ary/2){
                dir.push_back(1);
            }else{
                dir.push_back(-1);
                if(digit_my[i]==0)flg_is_wrap = true;
            } 
            most_sig_diff_bit = i;
        }
        else if(digit_my[i]>digit_dest[i]){
            if(digit_my[i]-digit_dest[i]<=num_ary/2){
                dir.push_back(-1);
            }else{
                dir.push_back(1);
                if(digit_my[i]==num_ary-1)flg_is_wrap = true;
            }
            most_sig_diff_bit = i;
        } else dir.push_back(0); //==
        is_wrap.push_back(flg_is_wrap);
    }
    assert(most_sig_diff_bit>=0);

    auto switch_allocator = m_router->getSwitchAllocator(); //What's this for?
    std::vector<int> vacant_nonstar_outports;
    std::vector<bool> vno_iswrap;
    for(int i = 0; i < num_dim; i++){
        if(digit_my[i] == digit_dest[i])continue;
        int outport = (dir[i] == 1) ? m_outports_dirn2idx["plus " + std::to_string(i)] : m_outports_dirn2idx["minus " + std::to_string(i)];
        DPRINTF(RubyResourceStalls, "Insert port to list for use %d corresponding bit %d\n", outport, i);
        if (switch_allocator->send_allowed_star(inport, invc, outport, -1, Star_type:: NONSTAR_) && (outport != 0) && !get_outport_used(outport)){
                vacant_nonstar_outports.push_back(outport);
                vno_iswrap.push_back(is_wrap[i]);
            }
    }
    if (vacant_nonstar_outports.size() != 0){
        int idx = rand() % vacant_nonstar_outports.size();
        int outport = vacant_nonstar_outports[idx];
        bool is_wrap = vno_iswrap[idx];
        Star_type Star = NONSTAR_;
        DPRINTF(RubyResourceStalls, "Use adaptive routing: Router %d: outport %d\n", my_id, outport);
        set_outport_used(outport, true);
        return std::make_pair(outport,std::make_pair(Star,is_wrap));
    }else{//star channel;
        int i = most_sig_diff_bit;
        int outport = (dir[i] == 1) ? m_outports_dirn2idx["plus " + std::to_string(i)] : m_outports_dirn2idx["minus " + std::to_string(i)];
        bool have_wrapped = (route.have_wrapped.find(i) != route.have_wrapped.end());
        Star_type Star = (have_wrapped | is_wrap[i]) ? STAR1_ : STAR0_;
        DPRINTF(RubyResourceStalls, "Use Dor port:%d outport_dim %s\n", outport, i);
        
        return std::make_pair(outport,std::make_pair(Star,is_wrap[i]));
    }
}

} // namespace garnet
} // namespace ruby
} // namespace gem5
