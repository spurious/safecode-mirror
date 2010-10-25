//===-- SCPoolHeuristic.cpp - SAFECode Pool Allocation Heuristic ----------===//
// 
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements a heuristic class that pool allocates a program
// according to SAFECode's requirements.
//
//===----------------------------------------------------------------------===//

#include "safecode/SCPoolHeuristic.h"

using namespace llvm;
using namespace PA;

//
// Function: GetNodesReachableFromGlobals()
//
// Description:
//  This function finds all DSNodes which are reachable from globals.  It finds
//  DSNodes both within the local DSGraph as well as in the Globals graph that
//  are reachable from globals.
//
// Inputs:
//  G - The DSGraph for which to find DSNodes which are reachable by globals.
//      This DSGraph can either by a DSGraph associated with a function *or*
//      it can be the globals graph itself.
//
// Outputs:
//  NodesFromGlobals - A reference to a container object in which to record
//                     DSNodes reachable from globals.  DSNodes are *added* to
//                     this container; it is not cleared by this function.
//                     DSNodes from both the local and globals graph are added.
static void
GetNodesReachableFromGlobals (DSGraph* G,
                              DenseSet<const DSNode*> &NodesFromGlobals) {
  //
  // Get the globals graph associated with this DSGraph.  If the globals graph
  // is NULL, then the graph that was passed in *is* the globals graph.
  //
  DSGraph * GlobalsGraph = G->getGlobalsGraph();
  if (!GlobalsGraph)
    GlobalsGraph = G;

  //
  // Find all DSNodes which are reachable in the globals graph.
  //
  for (DSGraph::node_iterator NI = GlobalsGraph->node_begin();
       NI != GlobalsGraph->node_end();
       ++NI) {
    NI->markReachableNodes(NodesFromGlobals);
  }

  //
  // Now the fun part.  Find DSNodes in the local graph that correspond to
  // those nodes reachable in the globals graph.  Add them to the set of
  // reachable nodes, too.
  //
  if (G->getGlobalsGraph()) {
    //
    // Compute a mapping between local DSNodes and DSNodes in the globals
    // graph.
    //
    DSGraph::NodeMapTy NodeMap;
    G->computeGToGGMapping (NodeMap);

    //
    // Scan through all DSNodes in the local graph.  If a local DSNode has a
    // corresponding DSNode in the globals graph that is reachable from a 
    // global, then add the local DSNode to the set of DSNodes reachable from a
    // global.
    //
    // FIXME: A node's existance within the global DSGraph is probably
    //        sufficient evidence that it is reachable from a global.
    //
    DSGraph::node_iterator ni = G->node_begin();
    for (; ni != G->node_end(); ++ni) {
      DSNode * N = ni;
      if (NodesFromGlobals.count (NodeMap[N].getNode()))
        NodesFromGlobals.insert (N);
    }
  }
}

//
// Method: findGlobalPoolNodes()
//
// Description:
//  This method finds DSNodes that are reachable from globals and that need a
//  pool.  The Automatic Pool Allocation transform will use the returned
//  information to build global pools for the DSNodes in question.
//
//  For efficiency, this method also determines which DSNodes should be in the
//  same pool.
//
// Outputs:
//  Nodes - The DSNodes that are both reachable from globals and which should
//          have global pools will be *added* to this container.
//
void
SCHeuristic::findGlobalPoolNodes (DSNodeSet_t & Nodes) {
  // Get the globals graph for the program.
  DSGraph* GG = Graphs->getGlobalsGraph();

  //
  // Get all of the nodes reachable from globals.
  //
  DenseSet<const DSNode*> GlobalNodes;
  GetNodesReachableFromGlobals (GG, GlobalNodes);

  //
  // Create a global pool for each global DSNode.
  //
  for (DenseSet<const DSNode *>::iterator NI = GlobalNodes.begin();
       NI != GlobalNodes.end();
       ++NI) {
    const DSNode * N = *NI;
    PoolMap[N] = OnePool(N);
  }

  //
  // Now find all DSNodes belonging to function-local DSGraphs which are
  // mirrored in the globals graph.  These DSNodes require a global pool, too,
  // but must use the same pool as the one assigned to the corresponding global
  // DSNode.
  //
  for (Module::iterator F = M->begin(); F != M->end(); ++F) {
    //
    // Ignore functions that have no DSGraph.
    //
    if (!(Graphs->hasDSGraph(*F))) continue;

    //
    // Compute a mapping between local DSNodes and DSNodes in the globals
    // graph.
    //
    DSGraph* G = Graphs->getDSGraph(*F);
    DSGraph::NodeMapTy NodeMap;
    G->computeGToGGMapping (NodeMap);

    //
    // Scan through all DSNodes in the local graph.  If a local DSNode has a
    // corresponding DSNode in the globals graph that is reachable from a 
    // global, then add the local DSNode to the set of DSNodes reachable from
    // a global.
    //
    DSGraph::node_iterator ni = G->node_begin();
    for (; ni != G->node_end(); ++ni) {
      DSNode * N = ni;
      DSNode * GGN = NodeMap[N].getNode();
      if (GGN && GlobalNodes.count (GGN))
        PoolMap[GGN].NodesInPool.push_back (N);
      else
        PoolMap[N] = OnePool(N);
    }
  }

  //
  // Scan through all the local graphs looking for DSNodes which may be
  // reachable by a global.  These nodes may not end up in the globals graph 
  // because of the fact that DSA doesn't actually know what is happening to
  // them.
  //
  // FIXME: I believe this code causes a condition in which a local DSNode is
  //        given a local pool in one function but not in other functions.
  //        Someone needs to investigate whether DSA is being consistent here,
  //        and if not, if that inconsistency is correct.
  //
#if 0
  for (Module::iterator F = M->begin(); F != M->end(); ++F) {
    if (F->isDeclaration()) continue;
    DSGraph* G = Graphs->getDSGraph(*F);
    for (DSGraph::node_iterator I = G->node_begin(), E = G->node_end();
         I != E;
         ++I) {
      DSNode * Node = I;
      if (Node->isExternalNode() || Node->isUnknownNode()) {
        GlobalNodes.insert (Node);
      }
    }
  }
#endif

  //
  // Copy the values into the output container.  Note that DenseSet has no
  // iterator traits (or whatever allows us to treat DenseSet has a generic
  // container), so we have to use a loop to copy values from the DenseSet into
  // the output container.
  //
  for (DenseSet<const DSNode*>::iterator I = GlobalNodes.begin(),
         E = GlobalNodes.end(); I != E; ++I) {
    Nodes.insert (*I);
  }

  return;
}

//
// Method: getLocalPoolNodes()
//
// Description:
//  For a given function, determine which DSNodes for that function should have
//  local pools created for them.
//
void
SCHeuristic::getLocalPoolNodes (const Function & F, DSNodeList_t & Nodes) {
  //
  // Get the DSGraph of the specified function.  If the DSGraph has no nodes,
  // then there is nothing we need to do.
  //
  DSGraph* G = Graphs->getDSGraph(F);
  if (G->node_begin() == G->node_end()) return;

  //
  // Calculate which DSNodes are reachable from globals.  If a node is reachable
  // from a global, we will create a global pool for it, so no argument passage
  // is required.
  Graphs->getGlobalsGraph();

  // Map all node reachable from this global to the corresponding nodes in
  // the globals graph.
  DSGraph::NodeMapTy GlobalsGraphNodeMapping;
  G->computeGToGGMapping(GlobalsGraphNodeMapping);

  //
  // Loop over all of the nodes which are non-escaping, adding pool-allocatable
  // ones to the NodesToPA vector.  In other words, scan over the DSGraph and
  // find nodes for which a new pool must be created within this function.
  //
  for (DSGraph::node_iterator I = G->node_begin(), E = G->node_end();
       I != E;
       ++I) {
    // Get the DSNode and, if applicable, its mirror in the globals graph
    DSNode * N   = I;
    DSNode * GGN = GlobalsGraphNodeMapping[N].getNode();

    //
    // We pool allocate all nodes.  Here, we just want to make sure that this
    // DSNode hasn't already been assigned to a global pool.
    //
    if (!(GlobalPoolNodes.count (N) || (GGN && GlobalPoolNodes.count (GGN)))) {
      // Otherwise, if it was not passed in from outside the function, it must
      // be a local pool!
      assert(!N->isGlobalNode() && "Should be in global mapping!");
      Nodes.push_back (N);
    }
  }

  return;
}

void
SCHeuristic::AssignToPools (const std::vector<const DSNode*> &NodesToPA,
                            Function *F, DSGraph* G,
                            std::vector<OnePool> &ResultPools) {
  for (unsigned i = 0, e = NodesToPA.size(); i != e; ++i)
    if (PoolMap.find (NodesToPA[i]) != PoolMap.end())
      ResultPools.push_back(PoolMap[NodesToPA[i]]);
}

//
// Method: releaseMemory()
//
// Description:
//  This method frees memory consumed by the pass when the pass is no longer
//  needed.
//
void
SCHeuristic::releaseMemory () {
  PoolMap.clear();
  GlobalPoolNodes.clear();
  return;
}

bool
SCHeuristic::runOnModule (Module & Module) {
  //
  // Remember which module we are analyzing.
  //
  M = &Module;

  //
  // Get the reference to the DSA Graph.
  //
  Graphs = &getAnalysis<EQTDDataStructures>();   
  assert (Graphs && "No DSGraphs!\n");

  //
  // Find DSNodes which are reachable from globals and should be pool
  // allocated.
  //
  findGlobalPoolNodes (GlobalPoolNodes);

  // We never modify anything in this pass
  return false;
}

static RegisterPass<SCHeuristic>
X ("paheur-sc", "Pool allocate for SAFECode heuristic");

RegisterAnalysisGroup<Heuristic> Heuristic(X);

char SCHeuristic::ID = 0;
