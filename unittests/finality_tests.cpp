#include "finality_test_cluster.hpp"

using namespace eosio::chain;

/*
 * register test suite `finality_tests`
 */
BOOST_AUTO_TEST_SUITE(finality_tests)

// verify LIB advances with 2 finalizers voting.
// ---------------------------------------------
BOOST_FIXTURE_TEST_CASE(two_votes, finality_test_cluster) { try {
   produce_and_push_block();
   for (auto i = 0; i < 3; ++i) {
      process_votes(1, num_needed_for_quorum);
      produce_and_push_block();

      // when a quorum of nodes vote, LIB should advance
      BOOST_REQUIRE_EQUAL(lib_advancing(), num_nodes);
   }
} FC_LOG_AND_RETHROW() }

// verify LIB does not advances with finalizers not voting.
// --------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(no_votes, finality_test_cluster) { try {
   BOOST_REQUIRE_EQUAL(lib_advancing(), 0);
   produce_and_push_block();
   for (auto i = 0; i < 3; ++i) {
      produce_and_push_block();
      // don't process votes

      // when only node0 votes, LIB shouldn't advance
      BOOST_REQUIRE_EQUAL(lib_advancing(), 0);
   }
} FC_LOG_AND_RETHROW() }


// verify LIB does not advances when one less than the quorum votes
// ----------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(quorum_minus_one, finality_test_cluster) { try {
   BOOST_REQUIRE_EQUAL(lib_advancing(), 0);
   produce_and_push_block();
   for (auto i = 0; i < 3; ++i) {
      produce_and_push_block();
      process_votes(1, num_needed_for_quorum - 1);

      // when one less than required vote, LIB shouldn't advance
      BOOST_REQUIRE_EQUAL(lib_advancing(), 0);
   }
} FC_LOG_AND_RETHROW() }


// verify LIB advances with all finalizers voting
// ----------------------------------------------
BOOST_FIXTURE_TEST_CASE(all_votes, finality_test_cluster) { try {
   produce_and_push_block();
   for (auto i = 0; i < 3; ++i) {
      process_votes(1, num_nodes - 1);
      produce_and_push_block();

      // when all nodes vote, LIB should advance
      BOOST_REQUIRE_EQUAL(lib_advancing(), num_nodes);
   }
} FC_LOG_AND_RETHROW() }

// verify LIB advances when votes conflict (strong first and followed by weak)
// ---------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(conflicting_votes_strong_first, finality_test_cluster) { try {
   produce_and_push_block();
   for (auto i = 0; i < 3; ++i) {
      auto next_idx = process_votes(1, num_needed_for_quorum);  // first a quorum of strong votes
      assert(next_idx < num_nodes);
      nodes[next_idx].process_vote(*this, -1, vote_mode::weak); // and one weak vote
      produce_and_push_block();

      // when we have a quorum of strong votes, one weak vote should not prevent LIB from advancing
      BOOST_REQUIRE_EQUAL(lib_advancing(), num_nodes);
   }
} FC_LOG_AND_RETHROW() }

// verify LIB advances when votes conflict (weak first and followed by strong)
// really not significant difference with previous test, just position of weak
// vote in bitset changes.
// ---------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(conflicting_votes_weak_first, finality_test_cluster) { try {
   produce_and_push_block();
   for (auto i = 0; i < 3; ++i) {
      node1.process_vote(*this, -1, vote_mode::weak);    // a weak vote on node 1
      process_votes(2, num_needed_for_quorum);           // and a quorum of strong votes
      produce_and_push_block();

      // when we have a quorum of strong votes, one weak vote should not prevent LIB from advancing
      BOOST_REQUIRE_EQUAL(lib_advancing(), num_nodes);
   }
} FC_LOG_AND_RETHROW() }

// Verify a delayed vote works
// ---------------------------
BOOST_FIXTURE_TEST_CASE(one_delayed_votes, finality_test_cluster) { try {
   // hold the vote for the first block to simulate delay
   produce_and_push_block();
   produce_and_push_block();

   // now node1 to nodeN each have a 2 vote vector
   // vote block 0 (index 0) to make it have a strong QC,
   // prompting LIB advancing on node2
   process_votes(1, num_needed_for_quorum, 0);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), num_nodes);

   // block 1 (index 1) has the same QC claim as block 0. It cannot move LIB
   process_votes(1, num_needed_for_quorum, 1);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), 0);

   // producing, pushing, and voting a new block makes LIB moving
   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), num_nodes);

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// Verify 3 consecutive delayed votes work
// ---------------------------------------
BOOST_FIXTURE_TEST_CASE(three_delayed_votes, finality_test_cluster) { try {
   // produce 4 blocks and hold the votes for the first 3 to simulate delayed votes
   // The 4 blocks have the same QC claim as no QCs are created because quorum was
   // not reached
   for (auto i = 0; i < 4; ++i)
      produce_and_push_block();

   // LIB did not advance
   BOOST_REQUIRE_EQUAL(lib_advancing(), 0);

   // vote block 0 (index 0) to make it have a strong QC,
   // prompting LIB advacing on nodes
   process_votes(1, num_needed_for_quorum, 0);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), num_nodes);

   // blocks 1 to 3 have the same QC claim as block 0. They cannot move LIB
   for (auto i=1; i < 4; ++i) {
      process_votes(1, num_needed_for_quorum, i);
      produce_and_push_block();
      BOOST_REQUIRE_EQUAL(lib_advancing(), 0);
   }

   // Now send votes for the last block that node0 produced (block 8). It will be
   // able to incorporate these votes into a new QC, which will be attached to
   // the next block it produces.
   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), num_nodes);

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// What happens when votes are processed out of order
// --------------------------------------------------
BOOST_FIXTURE_TEST_CASE(out_of_order_votes, finality_test_cluster) { try {
   // produce 3 blocks and hold the votes to simulate delayed votes
   // The 3 blocks have the same QC claim as no QCs are created because missing votes
   for (auto i = 0; i < 3; ++i)
      produce_and_push_block();

   // vote out of the order: the newest to oldest

   // vote block 2 (index 2) to make it have a strong QC,
   // prompting LIB advacing
   process_votes(1, num_needed_for_quorum, 2);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), num_nodes);

   // block 1 (index 1) has the same QC claim as block 2. It will not move LIB
   process_votes(1, num_needed_for_quorum, 1);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), 0);

   // block 0 (index 0) has the same QC claim as block 2. It will not move LIB
   process_votes(1, num_needed_for_quorum, 0);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), 0);

   // producing, pushing, and voting a new block makes LIB moving
   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), num_nodes);

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// Verify a vote which was delayed by a large number of blocks does not cause any issues
// -------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(long_delayed_votes, finality_test_cluster) { try {
   // Produce and push a block, vote on it after a long delay.
   constexpr uint32_t delayed_vote_index = 0;

   produce_and_push_block(); // this is the block we will vote on later
   produce_and_push_block();

   for (auto i = 2; i < 100; ++i) {
      process_votes(1, num_needed_for_quorum);
      produce_and_push_block();
      BOOST_REQUIRE_EQUAL(lib_advancing(), num_nodes);
   }

   // Late vote does not cause any issues
   BOOST_REQUIRE_NO_THROW(process_votes(1, num_needed_for_quorum, delayed_vote_index));

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// Check that if we never vote on a block, it doesn't cause any problem
// --------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(lost_votes, finality_test_cluster) { try {
   // Produce and push a block, never vote on it to simulate lost.
   // The block contains a strong QC extension for prior block
   auto b1 = produce_and_push_block();
   process_votes(1, num_needed_for_quorum);
   auto b2 = produce_and_push_block(); // this block contains a strong QC for the previous block
   const auto& ext = b2->template extract_extension<quorum_certificate_extension>();
   BOOST_REQUIRE_EQUAL(ext.qc.block_num, b1->block_num());

   // The strong QC extension for prior block makes LIB advance on nodes
   BOOST_REQUIRE_EQUAL(lib_advancing(), num_nodes);

   // but don't propagate the votes on b2. Mage sure they are lost
   clear_votes_and_reset_lib();

   produce_and_push_block();                // Produce another block
   BOOST_REQUIRE_EQUAL(lib_advancing(), 0); // LIB doesn't advance

   process_votes(1, num_needed_for_quorum); // and propagate the votes for this new block to node0
   produce_and_push_block();

   BOOST_REQUIRE_EQUAL(lib_advancing(), num_nodes); // vote causes lib to advance

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// One weak vote preventing a strong QC
// ------------------------------------
BOOST_FIXTURE_TEST_CASE(one_weak_vote, finality_test_cluster) { try {
   produce_and_push_block();

   auto next_idx = process_votes(1, num_needed_for_quorum -1); // one less strong vote than needed for quorum
   nodes[next_idx].process_vote(*this, -1, vote_mode::weak);   // and one weak vote
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), 0); // weak QC (1 shy of strong) => LIB does not advance

   process_votes(1, num_needed_for_quorum); // now this provides enough strong votes for quorum
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), num_nodes); // strong QC => LIB does advance

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// A quorum-1 of weak votes and one strong vote
// --------------------------------------------
BOOST_FIXTURE_TEST_CASE(quorum_minus_one_weak_vote, finality_test_cluster) { try {
   produce_and_push_block();

   process_votes(1, num_needed_for_quorum, -1, vote_mode::weak);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), 0); // weak QC => LIB does not advance

   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), num_nodes); // strong QC => LIB does advance

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// A sequence of "weak - strong - weak - strong" QCs
// -------------------------------------------------
BOOST_FIXTURE_TEST_CASE(weak_strong_weak_strong, finality_test_cluster) { try {
   produce_and_push_block();

   process_votes(1, num_needed_for_quorum, -1, vote_mode::weak);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), 0);           // weak QC => LIB does not advance

   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), num_nodes);  // strong QC => LIB does advance

   process_votes(1, num_needed_for_quorum, -1, vote_mode::weak);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), 0);          // weak QC => LIB does not advance

   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), num_nodes); // strong QC => LIB does advance

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// A sequence of "weak - weak - strong - strong" QCs
// -------------------------------------------------
BOOST_FIXTURE_TEST_CASE(weak_weak_strong_strong, finality_test_cluster) { try {
   produce_and_push_block();

   process_votes(1, num_needed_for_quorum, -1, vote_mode::weak);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), 0);         // weak QC => LIB does not advance

   process_votes(1, num_needed_for_quorum, -1, vote_mode::weak);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), 0);         // weak QC => LIB does not advance

   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), num_nodes); // strong QC => LIB does advance

   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), num_nodes); // strong QC => LIB does advance

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }


// Verify a combination of weak, delayed, lost votes still works
// -------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(weak_delayed_lost_vote, finality_test_cluster) { try {
   produce_and_push_block();

   // quorum of weak votes
   process_votes(1, num_needed_for_quorum, -1, vote_mode::weak);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), 0);

   // delay votes at index 1
   constexpr uint32_t delayed_index = 1; 
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), 0);

   // quorum of strong votes
   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), num_nodes);

   // A lost vote
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), 0);

   // The delayed vote arrives, does not advance lib
   process_votes(1, num_needed_for_quorum, delayed_index);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), 0);

   // strong vote advances lib
   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), num_nodes);

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// Verify a combination of delayed, weak, lost votes still work
// -------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(delayed_strong_weak_lost_vote, finality_test_cluster) { try {
   produce_and_push_block();

   // delay votes at index 1
   constexpr uint32_t delayed_index = 0;
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), 0);

   // quorum of strong votes
   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), num_nodes);

   // quorum of weak votes
   process_votes(1, num_needed_for_quorum, -1, vote_mode::weak);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), 0);

   // quorum of strong votes
   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), num_nodes);

   // A lost vote
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), 0);

   // The delayed vote arrives, does not advance lib
   process_votes(1, num_needed_for_quorum, delayed_index);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), 0);

   // strong vote advances lib
   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), num_nodes);

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// verify duplicate votes do not affect LIB advancing
// --------------------------------------------------
BOOST_FIXTURE_TEST_CASE(duplicate_votes, finality_test_cluster) { try {
   produce_and_push_block();

   for (auto i = 0; i < 5; ++i) {
      process_votes(1, num_needed_for_quorum, i, vote_mode::strong);

      // vote again (with duplicate == true) to make it duplicate
      process_votes(1, num_needed_for_quorum, i, vote_mode::strong, true);
      produce_and_push_block();

      // verify duplicate votes do not affect LIB advancing
      BOOST_REQUIRE_EQUAL(lib_advancing(), num_nodes);
   }

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

#if 0
// verify unknown_proposal votes are handled properly
// --------------------------------------------------
BOOST_FIXTURE_TEST_CASE(unknown_proposal_votes, finality_test_cluster) { try {
   produce_and_push_block();

   // intentionally corrupt block_id in node1's vote (vote index 0)
   node1.corrupt_vote_block_id();

   // process the corrupted vote
   BOOST_REQUIRE_THROW(process_votes(1, 1), fc::exception); // throws because it times out waiting on vote (block id not found)
   process_votes(2, num_needed_for_quorum - 1);

   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), 0);

   node1.restore_to_original_vote(0);                      // restore node1's vote at index 0 to original vote
   process_votes(1, 1, 0, vote_mode::strong);              // send restored vote to node0
   produce_and_push_block();                               // produce a block so the new QC can propagate
   BOOST_REQUIRE_EQUAL(lib_advancing(), num_nodes);

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }


// verify unknown finalizer_key votes are handled properly
BOOST_FIXTURE_TEST_CASE(unknown_finalizer_key_votes, finality_test_cluster) { try {
   // node0 produces a block and pushes to node1
   produce_and_push_block();

   // intentionally corrupt finalizer_key in node1's vote
   node1.corrupt_vote_finalizer_key();

   // process the corrupted vote. LIB should not advance
   node1.process_vote(*this, 0);
   BOOST_REQUIRE(node1.process_vote(*this, 0) == eosio::chain::vote_status::unknown_public_key);

   // restore to original vote
   node1.restore_to_original_vote(0);

   // process the original vote. LIB should advance
   node1.process_vote(*this, 0);

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// verify corrupted signature votes are handled properly
// -----------------------------------------------------
BOOST_FIXTURE_TEST_CASE(corrupted_signature_votes, finality_test_cluster) { try {
   produce_and_push_block();

   std::cout << node1.votes.size() << '\n';

   // intentionally corrupt signature in node1's vote (vote index 0)
   node1.corrupt_vote_signature();

   // process the corrupted vote
   process_votes(1, 1);
   process_votes(2, num_needed_for_quorum - 1);

   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(lib_advancing(), 0);           // because of the one corrupted vote, quorum is not reached

   node1.restore_to_original_vote(0);                 // restore node1's vote at index 0 to original vote
   process_votes(1, 1, 0, vote_mode::strong);         // send restored vote to node0
   produce_and_push_block();                          // produce a block so the new QC can propagate
   BOOST_REQUIRE_EQUAL(lib_advancing(), num_nodes);

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

#endif

BOOST_AUTO_TEST_SUITE_END()
