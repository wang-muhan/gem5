from m5.params import *
from m5.objects import *

from common import FileSystemConfig

from topologies.BaseTopology import SimpleTopology

# Creates a generic Mesh assuming an equal number of cache
# and directory controllers.
# XY routing is enforced (using link weights)
# to guarantee deadlock freedom.


class Torus(SimpleTopology):
    description = "Cube"

    def __init__(self, controllers):
        self.nodes = controllers

    # Makes a generic mesh
    # assuming an equal number of cache and directory cntrls

    def makeTopology(self, options, network, IntLink, ExtLink, Router):
        nodes = self.nodes

        num_routers = options.num_cpus
        num_dim = options.num_dim
        num_ary = options.num_ary
        assert num_ary > 2 # otherwise mesh

        link_latency = options.link_latency  # used by simple and garnet
        router_latency = options.router_latency  # only used by garnet

        cntrls_per_router, remainder = divmod(len(nodes), num_routers)

        # Create the routers in the mesh
        routers = [
            Router(router_id=i, latency=router_latency)
            for i in range(num_routers)
        ]
        network.routers = routers

        # link counter to set unique link ids
        link_count = 0

        # Add all but the remainder nodes to the list of nodes to be uniformly
        # distributed across the network.
        network_nodes = []
        remainder_nodes = []
        for node_index in range(len(nodes)):
            if node_index < (len(nodes) - remainder):
                network_nodes.append(nodes[node_index])
            else:
                remainder_nodes.append(nodes[node_index])

        # Connect each node to the appropriate router
        ext_links = []
        for (i, n) in enumerate(network_nodes):
            cntrl_level, router_id = divmod(i, num_routers)
            assert cntrl_level < cntrls_per_router
            ext_links.append(
                ExtLink(
                    link_id=link_count,
                    ext_node=n,
                    int_node=routers[router_id],
                    latency=link_latency,
                )
            )
            link_count += 1

        # Connect the remainding nodes to router 0.  These should only be
        # DMA nodes.
        for (i, node) in enumerate(remainder_nodes):
            assert node.type == "DMA_Controller"
            assert i < remainder
            ext_links.append(
                ExtLink(
                    link_id=link_count,
                    ext_node=node,
                    int_node=routers[0],
                    latency=link_latency,
                )
            )
            link_count += 1

        network.ext_links = ext_links

        # Create the mesh links.
        int_links = []

        #assert the number of routers is num_ary**num_dim
        assert num_ary**num_dim == num_routers, " num_ary ^ num_dimis not equal to num_routers"

        # # Create the adjacent links
        # for i in range(num_routers):
        #     for j in range(length):
        #         dest = i ^ (1 << j)
        #         # most significant bit first
        #         int_links.append(
        #             IntLink(link_id=link_count, src_node=routers[i], dst_node=routers[dest], latency=link_latency, weight=1,
        #             src_outport="msb {}".format(j),
        #             dst_inport="msb {}".format(j)
        #             )
        #         )
        #         link_count += 1

        # Create the adjacent links
        # 只能祈祷...
        for i in range(num_routers):
            tmpi = i
            pw = 1
            for j in range(num_dim):
                digit_j = tmpi % num_ary
                tmpi //= num_ary

                # plus:
                dest = i - digit_j *pw + (digit_j+1)%num_ary * pw
                print("plus",i,dest);
                int_links.append(
                    IntLink(link_id=link_count, src_node=routers[i], dst_node=routers[dest], latency=link_latency, weight=1,
                    src_outport="plus {}".format(j),
                    dst_inport="plus {}".format(j)
                    )
                )
                link_count += 1

                # plus:
                dest = i - digit_j *pw + (digit_j+num_ary-1)%num_ary * pw
                print("minus", i,dest);
                int_links.append(
                    IntLink(link_id=link_count, src_node=routers[i], dst_node=routers[dest], latency=link_latency, weight=1,
                    src_outport="minus {}".format(j),
                    dst_inport="minus {}".format(j)
                    )
                )
                link_count += 1

                pw = pw * num_ary

        network.int_links = int_links
        print("successfully made topology")
    # Register nodes with filesystem
    def registerTopology(self, options):
        for i in range(options.num_cpus):
            FileSystemConfig.register_node(
                [i], MemorySize(options.mem_size) // options.num_cpus, i
            )
