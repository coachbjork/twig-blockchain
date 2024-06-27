#pragma once

#include <eosio/chain/finality/finalizer_authority.hpp>
#include <fc/crypto/bls_private_key.hpp>

#include <eosio/testing/tester.hpp>
#include <ranges>
#include <boost/unordered/unordered_flat_map.hpp>

namespace savanna_cluster {
   namespace ranges = std::ranges;

   using vote_message_ptr = eosio::chain::vote_message_ptr;
   using vote_status      = eosio::chain::vote_status;
   using signed_block_ptr = eosio::chain::signed_block_ptr;
   using account_name     = eosio::chain::account_name;
   using finalizer_policy = eosio::chain::finalizer_policy;
   using digest_type      = eosio::chain::digest_type;
   using block_header     = eosio::chain::block_header;
   using tester           = eosio::testing::tester;
   using setup_policy     = eosio::testing::setup_policy;
   using bls_public_key   = fc::crypto::blslib::bls_public_key;
   template<class tester>
   using finalizer_keys   = eosio::testing::finalizer_keys<tester>;

   // ----------------------------------------------------------------------------
   template <class cluster_t>
   class cluster_node_t : public tester {
      uint32_t                prev_lib_num{0};
      size_t                  node_idx;
      cluster_t&              cluster;
      finalizer_keys<tester>  finkeys;
      size_t                  cur_key{0}; // index of key used in current policy

   public:
      cluster_node_t(size_t node_idx, cluster_t& cluster, setup_policy policy = setup_policy::none)
         : tester(policy)
         , node_idx(node_idx)
         , cluster(cluster)
         , finkeys(*this) {

         // since we are creating forks, finalizers may be locked on another fork and unable to vote.
         do_check_for_votes(false);

         control->voted_block().connect([&, node_idx](const eosio::chain::vote_signal_params& v) {
            // no mutex needed because controller is set in tester (via `disable_async_voting(true)`)
            // to vote (and emit the `voted_block` signal) synchronously.
            // --------------------------------------------------------------------------------------
            vote_status status = std::get<1>(v);
            if (status == vote_status::success)
               cluster.dispatch_vote_to_peers(node_idx, true, std::get<2>(v));
         });

         set_produce_block_callback(
            [&, node_idx](const signed_block_ptr& b) { cluster.push_block_to_peers(node_idx, true, b); });
      }

      void set_node_finalizers(size_t keys_per_node, size_t num_nodes) {
         finkeys.init_keys(keys_per_node * num_nodes, num_nodes);

         size_t first_node_key = node_idx * keys_per_node;
         cur_key               = first_node_key;
         finkeys.set_node_finalizers(first_node_key, keys_per_node);
      }

      std::pair<std::vector<bls_public_key>, eosio::chain::finalizer_policy>
      transition_to_savanna(std::span<const size_t> indices) {
         auto pubkeys = finkeys.set_finalizer_policy(indices).pubkeys;
         auto policy  = finkeys.transition_to_savanna();
         return { pubkeys, policy };
      }

      // returns true if LIB advances on this node since we last checked
      bool lib_advancing() {
         if (lib_num() > prev_lib_num) {
            prev_lib_num = lib_num();
            return true;
         }
         return false;
      }

      void reset_lib() { prev_lib_num = lib_num(); }

      uint32_t lib_num() const { return lib_block->block_num(); }

      uint32_t forkdb_head_num() const { return control->fork_db_head_block_num(); }

      signed_block_ptr forkdb_head() const { return control->fork_db_head_block(); }

      void push_blocks(cluster_node_t& to, uint32_t block_num_limit = std::numeric_limits<uint32_t>::max()) const {
         while (to.forkdb_head_num() < std::min(forkdb_head_num(), block_num_limit)) {
            auto sb = control->fetch_block_by_number(to.control->fork_db_head_block_num() + 1);
            to.push_block(sb);
         }
      }
   };

   // ---------------------------------------------------------------------------------------
   // cluster_t
   // ---------
   //
   // Set up a test network which consists of 4 nodes, all of which have transitioned to
   // the Savanna consensus.
   //
   // They are all finalizers (Each node has one finalizer) and can all produce blocks.
   // quorum is computed using the same formula as in the system contracts
   // (so quorum == 3)
   //
   //
   // By default they are all connected, receive all produced blocks, vote on them,
   // and send their votes to all other nodes.
   //
   // It is possible to split the 'virtual' network using `cluster_t::split()`.
   //  --------------------------------------------------------------------------------------
   template <size_t NUM_NODES, size_t KEYS_PER_NODE = 10> requires (NUM_NODES > 3)
   class cluster_t {
   public:
      using node_t = cluster_node_t<cluster_t>;

      cluster_t()
         : _nodes{
               {{0, *this, setup_policy::full_except_do_not_transition_to_savanna}, {1, *this}, {2, *this}, {3, *this}}
      } {
         // make sure we push node0 initialization (full_except_do_not_transition_to_savanna) to
         // the other nodes. Needed because the tester was initialized before `node_t`.
         // ------------------------------------------------------------------------------------
         for (size_t i = 0; i < _nodes.size(); ++i)
            node0.push_blocks(_nodes[i]);

         // from now on, propagation of blocks and votes happens automatically (thanks to the
         // callbacks registered in `node_t` constructor).
         //
         // Set one finalizer per node (keys at indices { 0, 10, 20, 30}) and create initial
         // `finalizer_policy` using these indices.
         // -----------------------------------------------------------------------------------

         // set initial finalizer policy
         // ----------------------------
         std::array<size_t, NUM_NODES> indices;

         for (size_t i = 0; i < _nodes.size(); ++i) {
            indices[i] = i * KEYS_PER_NODE;
            _nodes[i].set_node_finalizers(KEYS_PER_NODE, NUM_NODES);
         }

         // do the transition to Savanna on node0. Blocks will be propagated to the other nodes.
         // ------------------------------------------------------------------------------------
         auto [_fin_policy_pubkeys, fin_policy] = node0.transition_to_savanna(indices);

         // at this point, node0 has a QC to include in next block.
         // Produce that block and push it, but don't process votes so that
         // we don't start with an existing QC
         // ---------------------------------------------------------------
         node0.produce_block();

         // reset votes and saved lib, so that each test starts in a clean slate
         // --------------------------------------------------------------------
         reset_lib();
      }

      ~cluster_t() {
         _shutting_down = true;
      }

      // Create accounts and updates producers on node node_idx (producer updates will be
      // propagated to connected nodes), and wait until one of the new producers is pending.
      // return the index of the pending new producer (we assume no duplicates in producer list)
      // -----------------------------------------------------------------------------------
      size_t set_producers(size_t node_idx, const std::vector<account_name>& producers, bool create_accounts = true) {
         node_t& n = _nodes[node_idx];
         n.set_producers(producers);
         account_name pending;
         signed_block_ptr sb;
         while (1) {
            sb = n.produce_block();
            pending = n.control->pending_block_producer();
            if (ranges::any_of(producers, [&](auto a) { return a == pending; }))
               break;
         }
         return ranges::find(producers, pending) - producers.begin();
      }

      // provide a set of node indices which will be disconnected from other nodes of the network,
      // creating two separate networks.
      // within each of the two partitions, nodes are still fully connected
      // -----------------------------------------------------------------------------------------
      void set_partition(const std::vector<size_t>& indices) {
         auto inside = [&](size_t node_idx) {
            return ranges::any_of(indices, [&](auto i) { return i == node_idx; });
         };
         std::vector<size_t> complement;
         for (size_t i = 0; i < _nodes.size(); ++i)
            if (!inside(i))
               complement.push_back(i);

         auto set_peers = [&](const std::vector<size_t>& v) { for (auto i : v)  _peers[i] = v; };

         _peers.clear();
         set_peers(indices);
         set_peers(complement);
      }

      void set_partitions(std::initializer_list<std::vector<size_t>> l) {
         auto inside = [&](size_t node_idx) {
            return ranges::any_of(l, [node_idx](const auto& v) {
               return ranges::any_of(v, [node_idx](auto i) { return i == node_idx; }); });
         };

         std::vector<size_t> complement;
         for (size_t i = 0; i < _nodes.size(); ++i)
            if (!inside(i))
               complement.push_back(i);

         auto set_peers = [&](const std::vector<size_t>& v) { for (auto i : v)  _peers[i] = v; };

         _peers.clear();
         for (const auto& v : l)
            set_peers(v);
         set_peers(complement);
      }


      void push_blocks(node_t& node, const std::vector<size_t> &indices,
                       uint32_t block_num_limit = std::numeric_limits<uint32_t>::max()) {
         for (auto i : indices)
            node.push_blocks(_nodes[i], block_num_limit);
      }

      // After creating forks on different nodes on a partitioned network,
      // make sure that all chain heads of any node are also pushed to all other nodes
      void propagate_heads() {
         struct head_track { digest_type id; size_t node_idx; };
         std::vector<head_track> heads;

         // store all different chain head found in cluster into `heads` vector
         for (size_t i = 0; i < _nodes.size(); ++i) {
            auto& n = _nodes[i];
            auto head = n.head();
            if (!ranges::any_of(heads, [&](auto& h) { return h.id == head.id(); }))
               heads.emplace_back(head.id(), i);
         }

         for (auto& dest : _nodes) {
            for (auto& h : heads) {
               if (h.id == dest.head().id())
                  continue;

               // propagate blocks from `h.node_idx` to `dest`.
               // We assume all nodes have at least a parent irreversible block in common
               auto& src = _nodes[h.node_idx];
               std::vector<signed_block_ptr> push_queue;
               digest_type id = h.id;
               while (!dest.control->fetch_block_by_id(id)) {
                  auto sb = src.control->fetch_block_by_id(id);
                  assert(sb);
                  push_queue.push_back(sb);
                  id = sb->previous;
               }

               for (auto& b : push_queue | ranges::views::reverse)
                  dest.push_block(b);
            }
         }

      }

      // returns the number of nodes on which `lib` advanced since we last checked
      // -------------------------------------------------------------------------
      size_t num_lib_advancing() {
         return ranges::count_if(_nodes, [](node_t& n) { return n.lib_advancing(); });
      }

      void reset_lib() { for (auto& n : _nodes) n.reset_lib();  }

      void push_block(size_t dst_idx, const signed_block_ptr& sb) {
         push_block_to_peers(dst_idx, false, sb);
      }

      // push new blocks from src_idx node to all nodes in partition of dst_idx.
      void push_blocks(size_t src_idx, size_t dst_idx, uint32_t start_block_num) {
         auto& src = _nodes[src_idx];
         auto head_num = src.control->fork_db_head_block_num();

         for (uint32_t i=start_block_num; i<=head_num; ++i) {
            auto sb = src.control->fetch_block_by_number(i);
            push_block(dst_idx, sb);
         }
      }

      size_t num_nodes() const { return NUM_NODES; }

   public:
      std::array<node_t, NUM_NODES>   _nodes;

      node_t&                         node0 = _nodes[0];
      node_t&                         node1 = _nodes[1];
      node_t&                         node2 = _nodes[2];
      node_t&                         node3 = _nodes[3];

      std::vector<bls_public_key>     _fin_policy_pubkeys; // set of public keys for node finalizers

      static constexpr fc::microseconds _block_interval_us =
         fc::milliseconds(eosio::chain::config::block_interval_ms);

   private:
      using peers_t = boost::unordered_flat_map<size_t, std::vector<size_t>>;
      peers_t                         _peers;
      bool                            _shutting_down {false};

      friend node_t;

      void dispatch_vote_to_peers(size_t node_idx, bool skip_self, const vote_message_ptr& msg) {
         static uint32_t connection_id = 0;
         for_each_peer(node_idx, skip_self, [&](node_t& n) {
            n.control->process_vote_message(++connection_id, msg);
         });
      }

      void push_block_to_peers(size_t node_idx, bool skip_self, const signed_block_ptr& b) {
         for_each_peer(node_idx, skip_self, [&](node_t& n) {
            n.push_block(b);
         });
      }

      template<class CB>
      void for_each_peer(size_t node_idx, bool skip_self, const CB& cb) {
         if (_shutting_down)
            return;

         if (_peers.empty()) {
            for (size_t i=0; i<NUM_NODES; ++i)
               if (!skip_self || i != node_idx)
                  cb(_nodes[i]);
         } else {
            assert(_peers.find(node_idx) != _peers.end());
            const auto& peers = _peers[node_idx];
            for (auto i : peers)
               if (!skip_self || i != node_idx)
                  cb(_nodes[i]);
         }
      }

   };
}