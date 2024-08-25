#!/usr/bin/env python3
import os
import shutil
import signal

from TestHarness import Cluster, TestHelper, Utils, WalletMgr
from TestHarness.Node import BlockType

###############################################################
# production_restart
#
# Tests restart of a production node with a pending finalizer policy.
#
# Start up a network with two nodes. The first node is a producer node, defproducera that has vote-threads enabled. The
# second node has a producer (defproducerb) and a single finalizer key configured. Use the bios contract to transition
# to Savanna consensus while keeping the existing producers and using a finalizer policy with the two finalizers.
#
# Once everything has been confirmed to be working correctly and finality is advancing, cleanly shut down the producer
# defproducera node but keep the finalizer node of defproducerb running.
#
# Then change the finalizer policy (e.g. replace a key in node defproducera) to get the nodes into a state where
# they have a pending finalizer policy. At that point restart the producer node defproducera (with stale production
# enabled so it produces blocks again).
#
# The correct behavior is for votes from the finalizer node on the newly produced blocks to be accepted by producer
# node defproducera, QCs to be formed and included in new blocks, and finality to advance.
#
# Due to the bug in pre-1.0.0-rc1, we expect that on restart the producer node will reject the votes received by the
# finalizer node because the producer node will be computing the wrong finality digest.#
#
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

args=TestHelper.parse_args({"-d","--keep-logs","--dump-error-details","-v","--leave-running","--unshared"})
pnodes=2
delay=args.d
debug=args.v
prod_count = 1 # per node prod count
total_nodes=pnodes
dumpErrorDetails=args.dump_error_details

Utils.Debug=debug
testSuccessful=False

cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
walletMgr=WalletMgr(True, keepRunning=args.leave_running, keepLogs=args.keep_logs)

try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)

    Print(f'producing nodes: {pnodes}, delay between nodes launch: {delay} second{"s" if delay != 1 else ""}')

    Print("Stand up cluster")
    # For now do not load system contract as it does not support setfinalizer
    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, totalProducers=pnodes, delay=delay, loadSystemContract=False,
                      activateIF=True, topo="./tests/production_restart_test_shape.json") is False:
        errorExit("Failed to stand up eos cluster.")

    assert cluster.biosNode.getInfo(exitOnError=True)["head_block_producer"] != "eosio", "launch should have waited for production to change"
    cluster.biosNode.kill(signal.SIGTERM)
    cluster.waitOnClusterSync(blockAdvancing=5)

    producerNode = cluster.getNode(0)
    finalizerNode = cluster.getNode(1)

    Print("Wait for lib to advance")
    assert finalizerNode.waitForLibToAdvance(), "finalizerNode did not advance LIB"
    assert producerNode.waitForLibToAdvance(), "producerNode did not advance LIB"

    Print("Set finalizers so a pending is in play")
    # Try a number of times to make sure we have a pending policy
    numTrys = 10
    for i in range(0, numTrys):
       # Switch BLS keys on producerNode. setFinalizers uses the first key
       # in the configured key list (index 0)
       newBlsPubKey  = producerNode.keys[1].blspubkey
       newBlsPrivKey = producerNode.keys[1].blsprivkey
       newBlsPop     = producerNode.keys[1].blspop
       producerNode.keys[1].blspubkey  = producerNode.keys[0].blspubkey
       producerNode.keys[1].blsprivkey = producerNode.keys[0].blsprivkey
       producerNode.keys[1].blspop     = producerNode.keys[0].blspop
       producerNode.keys[0].blspubkey  = newBlsPubKey
       producerNode.keys[0].blsprivkey = newBlsPrivKey
       producerNode.keys[0].blspop     = newBlsPop

       assert cluster.setFinalizers([producerNode, finalizerNode], producerNode), "setfinalizers failed"
       assert producerNode.waitForLibToAdvance(), "producerNode did not advance LIB after setfinalizers"
       producerNode.waitForHeadToAdvance() # get additional qc

       # Check if a pending policy exists
       finalizerInfo = producerNode.getFinalizerInfo()
       Print(f"{finalizerInfo}")
       if finalizerInfo["payload"]["pending_finalizer_policy"] is not None and finalizerInfo["payload"]["pending_finalizer_policy"]["finalizers"][0]["public_key"] == newBlsPubKey:
          Print(f"Got a pending policy in {i+1} attempts")
          break
       else:
          Print(f"Trying to get a pending policy, {i+1} attempts")

    # It is OK if pending policy does not exist. During manual tests,
    # pending policy always exists at the first try
    Print("Shutdown producer producerNode")
    producerNode.kill(signal.SIGTERM)
    assert not producerNode.verifyAlive(), "producerNode did not shutdown"

    Print("Restart producer producerNode")
    producerNode.relaunch(chainArg=" -e ")

    Print("Verify LIB advances after restart")
    assert producerNode.waitForLibToAdvance(), "producerNode did not advance LIB"
    assert finalizerNode.waitForLibToAdvance(), "finalizerNode did not advance LIB"

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
