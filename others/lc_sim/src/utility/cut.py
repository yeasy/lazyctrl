#!/usr/bin/python
#coding:utf-8
'''
The cut module, which may call the external min kcut, balance cut, etc
@version: 1.0
@author: U{Baohua Yang<mailto:yangbaohua@gmail.com>}
@created: Oct 12, 2011
@last update: Dec 18, 2011
@see: U{<https://github.com/yeasy/lazyctrl>}
@TODO: nothing
'''
import os, random
import networkx as nx
from optparse import OptionParser

PROG_CUT = './mincut'

class Cut:
    def __init__(self):
        self.num_node = 0
        self.num_edge = 0
        self.num_group = 0
        self.groups = {}
        self.G = nx.Graph()

    def kcut(self, fn_in="sw_undirectional_affinity.txt", fn_metics="metis_input.txt"):
        """ 
        kcut with the given weighted sw_pair file, and combine into groups
        @param fn_in: Name of the input weight file
            format: src dst weight
        @param fn_metics: Name of the output file, which will be used in metics
        """
        if not fn_in:
            return
        for i in range(self.num_group):
            self.groups[i] = []
        f_in = open(fn_in, "r")
        f_metics = open(fn_metics, "w")
        src, dst, weight = 0, 0, 0
        edges = []
        i = 0
        try:
            self.num_node = int(f_in.readline())
            self.num_edge = int(f_in.readline())
            print "File says,nodes=%d, edges=%d" %(self.num_node,self.num_edge)
            for line in f_in.readlines():
                i += 1
                if i %10000 == 0:
                    print "read in %d lines." %i
                src, dst, weight = line.split()
                src, dst, weight = int(src), int(dst), int(weight)
                if (src,dst) in edges or (dst,src) in edges:
                    print "WARN, already in edges."
                edges.append((src, dst, weight))
        finally:
            #cutted_list = eval((os.popen(PROG_CUT + " " + fn_in)).read())
            #cutted_list = os.execvp(PROG_CUT, (PROG_CUT,) + tuple([fn_in]))
            #print cutted_list;
            #cutted_list = [170, 4, 2, 252, 58, 115, 200, 262, 74, 213, 260, 193, 3, 79, 233, 85, 100, 7, 210, 147, 171, 201, 99, 31, 103, 125, 96, 131, 84, 105, 179, 269, 107, 71, 143, 49, 247, 94, 253, 111, 197, 266, 81, 215, 8, 28, 267, 150, 37, 110, 78, 195, 67, 137, 186, 154, 230, 91, 272, 158, 153, 128, 10, 217, 207, 152, 146, 246, 212, 93, 245, 46, 163, 234, 180, 50, 119, 64, 117, 172, 156, 155, 13, 235, 258, 33, 178, 73, 126, 98, 175, 148, 41, 255, 218, 118, 109, 70, 225, 248, 270, 36, 224, 60, 124, 256, 254, 102, 47, 216, 151, 38, 19, 196, 183, 52, 185, 177, 211, 95, 167, 222, 141, 209, 226, 121, 23, 123, 34, 133, 75, 166, 181, 138, 227, 86, 27, 229, 136, 77, 190, 160, 228, 243, 134, 220, 16, 20, 108, 149, 250, 144, 1, 169, 142, 32, 145, 66, 130, 82, 271, 223, 14, 129, 61, 54, 219, 244, 240, 80, 6, 92, 40, 221, 232, 69, 88, 165, 202, 184, 26, 87, 188, 208, 17, 205, 161, 42, 12, 18, 187, 237, 65, 198, 251, 241, 97, 5, 239, 53, 268, 51, 44, 249, 113, 9, 194, 24, 11, 22, 173, 257, 164, 231, 204, 157, 191, 263, 45, 106, 236, 48, 72, 89, 112, 30, 261, 192, 83, 189, 62, 176, 199, 39, 57, 59, 114, 206, 15, 68, 35, 265, 242, 43, 101, 120, 29, 182, 168, 214, 56, 238, 139, 127, 90, 140, 203, 122, 21, 25, 55, 63, 76, 104, 116, 132, 135, 159, 162, 174, 259, 264]
            self.G.add_weighted_edges_from(edges)
            print "Actually, nodes=%d, edges=%d" %(len(self.G.nodes()),len(self.G.edges()))
            f_metics.write("%d %d 001\n" % (self.num_node,self.num_edge))
            i = 0;
            for n in self.G.nodes():
                i += len(self.G[n])
                for m in self.G[n]:
                    f_metics.write("%d %d " %(m,self.G.edge[n][m]['weight']))
                f_metics.write("\n")
            #print self.G.edges(data=True)
            print "bidirectional edges (2*edges) =",i
            f_in.close()
            f_metics.close()
        
    def bcut(self, node_set):
        """ balance cut
        @param node_set: The set of nodes to regroup
        """
        s = set()
        for _ in node_set:
            s.add(self.G.nodes(True)[_][1]['gid'])
        l = list(s)
        H = self.G.subgraph(node_set)
        FILE_TMP = 'tmp.txt'
        f = open(FILE_TMP, 'w')
        f.write("%d\n" % len(self.G.nodes()))
        f.write("%d\n" % len(H.edges()))
        try:
            for e in H.edges(data=True):
                f.write("%d %d %f\n" % (e[0], e[1], e[2]['weight']))
        finally:
            f.close()
            cutted_list = eval((os.popen(PROG_CUT + " " + FILE_TMP)).read())
            for i in range(len(cutted_list)):
                gid = l[i * len(l) / len(cutted_list)]
                self.G.nodes(True)[cutted_list[i]][1]['gid'] = gid

    def genAffinityFile(self, num_node=10, num_entry=100, max_weight=100, fn_out="test_affinity.txt"):
        """ Generate affinity sets for test inputing.
        @param num_node: Number of switches 
        @param num_entry: Number of affinity entries
        @param max_weight: Ignore here. Value of weight will stay inside [1,max_weight]
        @param fn_out: File to store the affnity records
        """
        lseed = []
        f = open('seed_affinity.txt', 'r')
        for seed in f.readlines():
            seed = int(seed)
            lseed.append(seed)
        G = nx.gnm_random_graph(num_node, num_entry)
        while not nx.is_connected(G):
            G = nx.gnm_random_graph(num_node, num_entry)
        f = open(fn_out, 'w')
        f.write("%u\n" % num_node)
        f.write("%u\n" % num_entry)
        try:
            for e in G.edges():
                f.write("%u %u %u\n" % (e[0] + 1, e[1] + 1, random.choice(lseed)))
        finally:
            f.close()

if __name__ == "__main__":
    parser = OptionParser(usage="usage: %prog [-i FILE][-o FILE][-m KCUT|BCUT|GEN]", version="%prog 1.0")
    parser.add_option("-i", "--input_file",
            action="store", type="string",
            dest="input", default="",
            help="read weight data from FILE", metavar="FILE")
    parser.add_option("-o", "--output_file",
            action="store", type="string",
            dest="output", default="",
            help="write result into FILE", metavar="FILE")
    parser.add_option("-m", "--mode",
            action="store", type="string",
            dest="mode", default="kcut",
            help="indicate the mode, \
                      KCUT will finish the kcut process with given file, \
                      BCUT will test the balance cut",
                      metavar="MODE")
    (opt, args) = parser.parse_args()
    if not opt.input:
        print "No input file given, pls indicate as -i INPUT_FILE"
        exit()
    if not opt.output:
        opt.output = "metics_input.txt"
    c = Cut()
    if opt.mode.upper() == 'KCUT': #kcut
        c.kcut(opt.input,opt.output)
    elif opt.mode.upper() == 'BCUT': #balance cut, only for testing here
        c.bcut([])
    elif opt.mode.upper() == 'GEN': #generate a test affinity file
        s = 72
        n = 2 * s 
        e = n * 2995.0 / 136
        c.genAffinityFile(n, e, 10000, "test_affinity_%u_%u" % (n, e))
