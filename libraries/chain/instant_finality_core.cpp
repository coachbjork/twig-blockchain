using block_num_type  = uint32_t;
using block_time_type = chain::block_timestamp_type;

struct block_ref
{
   block_id_type    block_id;
   block_time_type  timestamp;

   block_num_type block_num() const; // Extract from block_id.
};

struct qc_link
{
   block_num_type  source_block_num;
   block_num_type  target_block_num; // Must be less than or equal to source_block_num (only equal for genesis block).
   bool            is_link_strong;
};

struct qc_claim
{
   block_num_type  block_num;
   bool            is_strong_qc;
};

struct core
{
   std::vector<qc_link>    links; // Captures all relevant links sorted in order of ascending source_block_num.
   std::vector<block_ref>  refs; // Covers ancestor blocks with block numbers greater than or equal to last_final_block_num.
                                 // Sorted in order of ascending block_num.
   block_num_type          final_on_strong_qc_block_num;

   // Invariants:
   // 1. links.empty() == false
   // 2. last_final_block_num() <= final_on_strong_qc_block_num <= latest_qc_claim().block_num
   // 3. If refs.empty() == true, then (links.size() == 1) and 
   //                                  (links.back().target_block_num == links.back().source_block_num == final_on_strong_qc_block_num == last_final_block_num())
   // 4. If refs.empty() == false, then refs.front().block_num() == links.front().target_block_num == last_final_block_num()
   // 5. If refs.empty() == false, then refs.back().block_num() + 1 == links.back().source_block_num == current_block_num()
   // 6. If refs.size() > 1, then:
   //       For i = 0 to refs.size() - 2: 
   //          (refs[i].block_num() + 1 == refs[i+1].block_num()) and (refs[i].timestamp < refs[i+1].timestamp)
   // 7. If links.size() > 1, then:
   //       For i = 0 to links.size() - 2:
   //          (links[i].source_block_num + 1 == links[i+1].source_block_num) and (links[i].target_block_num <= links[i+1].target_block_num)
   // 8. current_block_num() - last_final_block_num() == refs.size() (always implied by invariants 3 to 6)
   // 9. current_block_num() - links.front().source_block_num == links.size() - 1 (always implied by invariants 1 and 7)

   static core create_core_for_genesis_block(block_num_type block_num)
   {
      return {
         .links                        = {
            qc_link{
               .source_block_num = block_num,
               .target_block_num = block_num,
               .is_strong_qc     = false,
            },
         },
         .refs                         = {},
         .final_on_strong_qc_block_num = block_num,
         .last_final_block_num         = block_num,
      };

      // Invariants 1 to 7 can be easily verified to be satisfied for the returned core. 
      // (And so, remaining invariants are also automatically satisfied.)
   }

   block_num_type current_block_num() const
   {
      assert(!links.empty()); // Satisfied by invariant 1.

      return links.back().source_block_num;
   }

   block_num_type last_final_block_num() const
   {
      assert(!links.empty()); // Satisfied by invariant 1.

      return links.front().target_block_num;
   }


   qc_claim latest_qc_claim() const
   {
      assert(!links.empty()); // Satisfied by invariant 1.

      return qc_claim{.block_num = links.back().target_block_num, .is_strong_qc = links.back().is_link_strong};
   }
   /**
    *  @pre last_final_block_num() <= block_num < current_block_num()
    *
    *  @post returned block_ref has block_num() == block_num
    */
   const block_ref& get_block_reference(block_num_type block_num) const
   {
      assert(last_final_block_num() <= block_num); // Satisfied by precondition.
      assert(block_num < current_block_num()); // Satisfied by precondition.

      // If refs.empty() == true, then by invariant 3, current_block_num() == last_final_block_num(), 
      // and therefore it is impossible to satisfy the precondition. So going forward, it is safe to assume refs.empty() == false.

      const size_t ref_index = block_num - last_final_block_num();

      // By the precondition, 0 <= ref_index < (current_block_num() - last_final_block_num()).
      // Then, by invariant 8, 0 <= ref_index < refs.size().

      assert(ref_index < refs.size()); // Satisfied by justification above.

      return tail[ref_index];
      // By invariants 4 and 6, tail[ref_index].block_num() == block_num, which satisfies the post-condition.
   }

   /**
    *  @pre links.front().source_block_num <= block_num <= current_block_num()
    *
    *  @post returned qc_link has source_block_num == block_num
    */
   const qc_link& get_qc_link_from(block_num_type block_num) const
   {
      assert(!links.empty()); // Satisfied by invariant 1.

      assert(links.front().source_block_num <= block_num); // Satisfied by precondition.
      assert(block_num <= current_block_num()); // Satisfied by precondition.

      const size_t link_index = block_num - links.front().source_block_num;

      // By the precondition, 0 <= link_index <= (current_block_num() - links.front().source_block_num).
      // Then, by invariant 9, 0 <= link_index <= links.size() - 1

      assert(link_index < link.size()); // Satisfied by justification above.

      return links[link_index];
      // By invariants 7, links[link_index].source_block_num == block_num, which satisfies the post-condition.
   }

   /**
    *  @pre current_block.block_num() == this->current_block_num()
    *  @pre If this->refs.empty() == false, then current_block is the block after the one referenced by this->refs.back()
    *  @pre this->latest_qc_claim().block_num <= most_recent_ancestor_with_qc.block_num <= this->current_block_num()
    *  @pre this->latest_qc_claim() <= most_recent_ancestor_with_qc
    *
    *  @post returned core has current_block_num() == this->current_block_num() + 1
    *  @post returned core has latest_qc_claim() == most_recent_ancestor_with_qc
    *  @post returned core has final_on_strong_qc_block_num >= this->final_on_strong_qc_block_num
    *  @post returned core has last_final_block_num() >= this->last_final_block_num()
    */
   core next(const block_ref& current_block, const qc_claim& most_recent_ancestor_with_qc) const
   {
      assert(current_block.block_num() == current_block_num()); // Satisfied by precondition 1.
      
      assert(refs.empty() || (refs.back().timestamp < current_block.timestamp)); // Satisfied by precondition 2.
      assert(refs.empty() || (refs.back().block_num() + 1 == current_block.block_num())); // Satisfied by precondition 2.

      assert(most_recent_ancestor_with_qc.block_num <= current_block_num()); // Satisfied by precondition 3.

      assert(latest_qc_claim() <= most_recent_ancestor_with_qc) // Satisfied by precondition 4.

      core next_core;

      auto new_block_nums = [&]() -> std::pair<block_num_type, block_num_type> 
         {
            assert(last_final_block_num() <= final_on_strong_qc_block_num); // Satisfied by invariant 2.

            if (!most_recent_ancestor_with_qc.is_strong_qc) {
               return {last_final_block_num(), final_on_strong_qc_block_num};
            }

            if (most_recent_ancestor_with_qc.block_num < links.front().source_block_num) {
               return {last_final_block_num(), final_on_strong_qc_block_num};
            }

            const auto& link1 = get_qc_link_from(most_recent_ancestor_with_qc.block_num);

            // TODO: Show the following hold true:
            // final_on_strong_qc_block_num <= link1.target_block_num <= current_block_num().
            // link1.target_block_num == current_block_num() iff refs.empty() == true.

            // Since last_final_block_num() <= final_on_strong_qc_block_num 
            // and final_on_strong_qc_block_num <= link1.target_block_num,
            // then last_final_block_num() <= link1.target_block_num.

            if (!link1.is_link_strong) {
               return {last_final_block_num(), link1.target_block_num};
            }

            if (link1.target_block_num < links.front().source_block_num) {
               return {last_final_block_num(), link1.target_block_num};
            }

            const auto& link2 = get_qc_link_from(link1.target_block_num);

            // TODO: Show the following hold true:
            // last_final_block_num() <= link2.target_block_num
            // link2.target_block_num <= link1.target_block_num
            // link1.target_block_num <= most_recent_ancestor_with_qc.block_num

            return {link2.target_block_num, link1.target_block_num}; 
         };

      const auto [new_last_final_block_num, new_final_on_strong_qc_block_num] = new_block_nums();

      assert(new_last_final_block_num <= new_final_on_strong_qc_block_num); // Satisfied by justification in new_block_nums.
      assert(new_final_on_strong_qc_block_num <= most_recent_ancestor_with_qc.block_num); // Satisfied by justification in new_block_nums.

      assert(final_on_strong_qc_block_num <= new_final_on_strong_qc_block_num); // Satisfied by justifications in new_block_nums.
      assert(last_final_block_num <= new_last_final_block_num); // Satisfied by justifications in new_block_nums.

      next_core.final_on_strong_qc_block_num = new_final_on_strong_qc_block_num;
      // Post-condition 3 is satisfied, assuming next_core will be returned without further modifications to next_core.final_on_strong_qc_block_num.

      // Post-condition 4 and invariant 2 will be satisfied when next_core.last_final_block_num() is updated to become new_last_final_block_num.

      // Setup next_core.links by garbage collecting unnecessary links and then adding the new QC link.
      {
         size_t links_index = 0; // Default to no garbage collection (if last_final_block_num does not change).

         if (last_final_block_num < next_core.last_final_block_num) {
            // new_blocks_nums found the new_last_final_block_num from a link that had a source_block_num
            // equal to new_final_on_strong_qc_block_num.
            // The index within links was (new_final_on_strong_qc_block_num - last_final_block_num).
            // All prior links can be garbage collected.

            links_index = new_final_on_strong_qc_block_num - last_final_block_num;

            assert(links_index < links.size()); // Satisfied by justification in this->get_qc_link_from(next_core.final_on_strong_qc_block_num).
         }

         next_core.links.reserve(links.size() - links_index + 1);

         // Garbage collect unnecessary links
         std::copy(links.cbegin() + links_index, links.cend(), std::back_inserter(next_core.links));

         assert(next_core.last_final_block_num() == new_last_final_block_num); // Satisfied by choice of links_index.

         // Also, by choice of links_index, at this point, next_core.links.back() == this->links.back().
         assert(next_core.links.back().source_block_num == current_block_num()); // Satisfied because last item in links has not yet changed.
         assert(next_core.links.back().target_block_num <= most_recent_ancestor_with_qc.block_num); // Satisfied because of above and precondition 3.

         // Add new link
         next_core.links.emplace_back(
            qc_link{
               .source_block_num = current_block_num() + 1, 
               .target_block_num = most_recent_ancestor_with_qc.block_num, // Guaranteed to be less than current_block_num() + 1.
               .is_link_strong = most_recent_ancestor_with_qc.is_strong_qc,
            });

         // Post-conditions 1, 2, and 4 are satisfied, assuming next_core will be returned without further modifications to next_core.links.

         // Invariants 1, 2, and 7 are satisfied for next_core.
      }

      // Setup next_core.refs by garbage collecting unnecessary block references in the refs and then adding the new block reference.
      {
         const size_t refs_index = next_core.last_final_block_num - last_final_block_num;

         // Using the justifications in new_block_nums, 0 <= ref_index <= (current_block_num() - last_final_block_num).
         // If refs.empty() == true, then by invariant 3, current_block_num() == last_final_block_num, and therefore ref_index == 0.
         // Otherwise if refs.empty() == false, the justification in new_block_nums provides the stronger inequality
         // 0 <= ref_index < (current_block_num() - last_final_block_num), which, using invariant 8, can be simplified to
         // 0 <= ref_index < refs.size().

         assert(!refs.empty() || (refs_index == 0)); // Satisfied by justification above.
         assert(refs.empty() || (refs_index < refs.size())); // Satisfied by justification above.

         next_core.refs.reserve(refs.size() - ref_index + 1);

         // Garbage collect unnecessary block references
         std::copy(refs.cbegin() + refs_index, refs.cend(), std::back_inserter(next_core.refs));

         assert(refs.empty() || (refs.front().block_num() == new_last_final_block_num)); // Satisfied by choice of refs_index.

         // Add new block reference
         next_core.tail.emplace_back(current_block);

         // Invariant 3 is trivially satisfied for next_core because next_core.refs.empty() == false.

         // Invariant 5 is clearly satisfied for next_core because next_core.refs.back().block_num() == this->current_block_num()
         // and next_core.links.back().source_block_num == this->current_block_num() + 1.

         // Invariant 6 is also clearly satisfied for next_core because invariant 6 is satisfied for *this and the only
         // additional requirements needed are the ones provided by precondition 2.

         // If this->refs.empty() == true, then new_last_final_block_num == last_final_block_num == current_block_num(),
         // and next_core.refs.size() == 1 and next_core.front() == current_block.
         // And so, next_core.front().block_num() == new_last_final_block_num.
         // If this->refs.empty() == false, then adding the current_block to the end does not change the fact that
         // refs.front().block_num() is still equal to new_last_final_block_num.

         assert(refs.front().block_num() == new_last_final_block_num)); // Satisfied by justification above.

         // Because it was also already shown earlier that links.front().target_block_num == new_last_final_block_num,
         // then the justification above satisfies the remaining equalities needed to satisfy invariant 4 for next_core.

         // So, invariants 3 to 6 are now satisfied for next_core in addition to the invariants 1, 2, and 7 that were shown to be satisfied
         // earlier (and still remain satisfied since next_core.links and next_core.final_on_strong_qc_block_num have not changed).
      }

      return next_core;
      // Invariants 1 to 7 were verified to be satisfied for the current value of next_core at various points above. 
      // (And so, the remaining invariants for next_core are also automatically satisfied.)
   }
};

//--------------

// Note: The code below is not fully fleshed out and is not fully compatible with the way the Leap implementation will actually handle this.

struct block_metadata
{
   block_id_type    block_id;
   block_time_type  timestamp;
   digest_type      finality_digest;

   operator block_ref() const 
   {
      return block_ref{.block_id = block_id, .timestamp = timestamp};
   }

   block_num_type block_num() const
   {
      return static_cast<block_ref>(*this).block_num();
   }
};

struct finalizer_policy
{
   uint64_t generation;
   // ... other fields ...

   digest_type compute_digest() const;
};

struct minimal_state
{
   uint32_t                  protocol_version;
   core                      state_core;
   block_metadata            latest_block_metadata;
   std::vector<digest_type>  validity_mroots; // Covers validated ancestor blocks (in order of ascending block number) with block 
                                              // numbers greater than or equal to state_core.final_on_strong_qc_block_num.
   std::vector<digest_type>  finality_digests; // Covers ancestor blocks (in order of ascending block number) with block 
                                               // numbers greater than or equal to state_core.latest_qc_claim().block_num.

   std::shared_ptr<finalizer_policy> active_finalizer_policy;

   // Invariants:
   // 1. state_core.current_block_num() == latest_block_metadata.block_num()
   // 2. If state_core.refs.empty() == false, state_core.refs.back().timestamp < latest_block_metadata.timestamp
   // 3. state_core.final_on_strong_qc_block_num + validity_mroot.size() == state_core.latest_qc_claim().block_num + 1

   static digest_type compute_finalizer_digest(uint32_t protocol_version, 
                                               const finalizer_policy& active_finalizer_policy, 
                                               digest_type finality_mroot,
                                               digest_type base_digest)
   {
      // Calculate static_data_digest which is the SHA256 hash of (active_finalizer_policy.compute_digest(), base_digest).
      // Then calculate and return the SHA256 hash of (protocol_version, active_finalizer_policy.generation, finality_mroot, static_data_digest).
   }

   /**
    *  @pre header.protocol_version() == 0
    *  @pre this->latest_block_metadata.timestamp < next_timestamp
    *  @pre this->state_core.latest_qc_claim().block_num <= most_recent_ancestor_with_qc.block_num <= this->state_core.current_block_num()
    *  @pre this->state_core.latest_qc_claim() <= most_recent_ancestor_with_qc
    *  @pre additional_finality_mroots covers ancestor blocks with block number starting from this->state_core.latest_qc_claim().block_num()
    *       and ending with most_recent_ancestor_with_qc.block_num()
    */
   minimal_state next(block_header header, 
                      std::vector<std::pair<block_num_type, digest_type>> additional_validity_mroots, 
                      const qc_claim& most_recent_ancestor_with_qc) const
   {
      minimal_state next_state;

      next_state.protocol_version = header.protocol_version();
      assert(next_state.protocol_version == 0); // Only version 0 is currently supported.

      next_state.core = state_core.next(latest_block_metadata, most_recent_ancestor_with_qc);

      const size_t vmr_index = next_state.core.final_on_strong_qc_block_num - state_core.final_on_strong_qc_block_num;

      assert(additional_validity_mroots.size() == vmr_index);
      assert(vmr_index < validity_mroots.size());
      
      const auto& finality_mroot = validity_mroots[vmr_index];

      next_state.latest_block_metadata = 
         block_metadata{
            .block_id        = header.calculate_id(),
            .timestamp       = header.timestamp,
            .finality_digest = compute_finalizer_digest(next_state.protocol_version, *active_finalizer_policy, finality_mroot, base_digest),
         };

      {
         next_state.validity_mroots.reserve(validity_mroots.size() - vmr_index + additional_validity_mroots.size());
         std::copy(validity_mroots.cbegin() + vmr_index, validity_mroots.cend(), std::back_inserter(next_state.validity_mroots));

         const auto end_block_num = next_state.core.latest_qc_claim().block_num;
         block_num_type expected_block_num = state_core.latest_qc_claim().block_num;
         auto itr = additional_validity_mroots.cbegin();
         for (; expected_block_num < end_block_num; ++expected_block_num, ++itr) {
            assert(itr->first == expected_block_num);
            validity_mroots.emplace_back(itr->second);
         }
      }

      {
         const size_t fd_index = next_state.core.latest_qc_claim().block_num - state_core.latest_qc_claim().block_num;
         next_state.finality_digests.reserve(finality_digests.size() - fd_index + 1);
         std::copy(finality_digests.cbegin() + fd_index, finality_digests.cend(), std::back_inserter(next_state.finality_digests));
         next_state.finality_digests.emplace_back(latest_block_metadata.finality_digest);
      }

      return next_state;
   }
};
