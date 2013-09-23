#!/usr/bin/python
'''The Topology module for the HC project.
@version: 1.0
@author: U{Baohua Yang<mailto:yangbaohua@gmail.com>}
@created: Oct 12, 2011
@last update: Oct 22, 2011
@see: U{<https://github.com/yeasy/lazyctrl>}
@TODO: nothing
'''
import networkx as nx
import os.path, sys
from matplotlib import pyplot as plt

class Topo:
    """ Topology class.
    """
    def __init__(self, name, num_port=16):
        """ Generate a topo.
        @param name: Topology name
        @param num_port: Number of ports of each switch
        """
        self.name = name
        self.G = nx.Graph()
        self.num_port = num_port
        self.coreSWList, self.aggSWList, self.edgeSWList = [], [], []

    def importFrom(self,fn):
        """
        Import the topo from outside file.
        @param fn: Name of the outside file
        """
        if not fn:
            return
        f = open(fn,'r')
        try:
            for l in f.readlines():
                src,dst,w = l.split()
                src,dst,w = int(src),int(dst),float(w)
                if src not in self.edgeSWList:
                    self.edgeSWList.append(src)
                if dst not in self.edgeSWList:
                    self.edgeSWList.append(dst)
                self.G.add_edge(src,dst,weight=w) #add new edge
        except:
            print "[Topo.importFrom()]Error in open file",fn
        finally:
            f.close()
        
    def info(self):
        """ Print information of the topology.
        """
        print "Name=%s" % self.name
        print "#Nodes=%u" % len(self.G.nodes())
        print "#Edges=%u" % len(self.G.edges())

    def exportPng(self, fn='test.png'):
        """ Save the topo into a png file.
        @param fn: The name of exported file
        """
        G = self.G
        if not fn:
            fn = self.name
        if G.is_directed():
            test_graph = G.to_undirected()
        else:
            test_graph = G
        fn_out_png = os.path.splitext(fn)[0] + ".png"
        plt.figure(figsize=(8, 8))
        #pos = nx.graphviz_layout(test_graph, prog='neato')
        #pos = nx.spring_layout(test_graph)
        pos = nx.shell_layout(test_graph)
        nx.draw(test_graph, pos, alpha=1.0, node_size=160, node_color='#eeeeee')
        #xmax = 1.1 * max(xx for xx, yy in pos.values())
        #ymax = 1.1 * max(yy for xx, yy in pos.values())
        #plt.xlim(-1*xmax, xmax)
        #plt.ylim(-1*xmax, ymax)
        plt.savefig(fn_out_png)

    def exportSW(self, k, switchfile='switch.txt'):
        """ Save the switch information into switch.txt.
        @param k: number of ports of each edge switch
        @param switchfile: out file name
        """
        host_id = 0 #start with 0
        with open(switchfile, 'w') as f:
            try:
                for sw in self.aggSWList:
                    for i in range(k):
                        f.write("%u %u\n" % (host_id, sw))
                        host_id += 1
            except:
                    ex = sys.exc_info()[2].tb_frame.f_back
                    print "[file %s, line %s, module %s]: Error when exportSW to %s, " \
                    % (__file__, ex.f_lineno, __name__, switchfile)
        f.close()

class Random(Topo):
    """ Random Topology.
    """
    def __init__(self, n, m, seed=None):
        """ Produces a graph picked randomly 
        out of the set of all graphs 
        with n nodes and m edges.
        @param n: Number of nodes
        @param m: Number of edges
        @param seed: Seed for random number generator (default=None).
        @return: The Topology
        """
        Topo.__init__(self, name='Random')
        self.n = n
        self.m = m
        self.G = nx.gnm_random_graph(n, m)

class Flat(Topo):
    """ Flat Topology with only edge switches.
    """
    def __init__(self, num_sw=16, num_port=4):
        """ Produces a flat topo with only edge switches.
        @param num_sw: The number of switches
        @param num_port: The number of ports of each switch
        @return: The Topology
        """
        Topo.__init__(self, name='Flat', num_port=num_port)
        self.edgeSWList = range(num_sw)
        self.G.add_nodes_from(self.edgeSWList, type='edge_switch') #edge sw

class FatTree(Topo):
    """ FatTree Topology.
    """
    def __init__(self, k):
        """ Generate the topo.
        @param k: k-port switch supports (k^2)*5/4 sws and (k^3)/4 hosts
        """
        Topo.__init__(self, name='FatTree', num_port=k)
        self.coreSWList = [i for i in range(k ** 2 / 4)]
        self.G.add_nodes_from(self.coreSWList, type='core_switch') #core sw
        self.aggSWList = [i + len(self.coreSWList) for i in range(k ** 2 / 2)]
        self.G.add_nodes_from(self.aggSWList, type='agg_switch') #agg sw
        self.edgeSWList = [i + len(self.coreSWList) + len(self.aggSWList) for i in range(k ** 2 / 2)]
        self.G.add_nodes_from(self.edgeSWList, type='edge_switch') #edge sw
        for p in range(k): #for pod p
            for i in range(k / 2): #for each agg switch
                for j in range(k / 2): #for each port of the agg switch
                    self.G.add_edge(self.aggSWList[i + p * (k / 2)], self.coreSWList[j + i * (k / 2)])
                for j in range(k / 2): #for each edge switch in the pod
                    self.G.add_edge(self.aggSWList[i + p * (k / 2)], self.edgeSWList[j + p * (k / 2)])

if __name__ == "__main__":
    #Random(10,20).info()
    #Topo('test').info()
    #fattree = FatTree(48)
    #fattree.info()
    #fattree.exportSW(48, 'testSW.txt')
    #fattree.export('test.png')
    flat = Flat()
    flat.info()
    topo = Topo('test')
    topo.importFrom("./sw_pair_weight.txt")
    topo.exportPng('sw_pair_weight.png')
