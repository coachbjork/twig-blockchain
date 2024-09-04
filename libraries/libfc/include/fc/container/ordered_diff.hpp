#pragma once

#include <fc/exception/exception.hpp>
#include <fc/utility.hpp>

#include <vector>
#include <utility>

namespace fc {

/**
 * @class ordered_diff
 * @brief Provides ability to generate and apply diff of containers of type T
 *
 * NOTE: Part of Spring Consensus. Used for finalizer and proposer policies.
 *
 * Example use:
 *    std::vector<char> source = { 'a', 'b', 'f', 'c', 'd' };
 *    std::vector<char> target = { 'b', 'f', 'c', 'd', 'e', 'h' };
 *    ordered_diff<char>::diff_result diff = ordered_diff<char>::diff(source, target);
 *    ordered_diff<char>::apply_diff(source, std::move(diff));
 *    assert(source == target);
 *
 * @param T type stored in Container, must provide ==
 * @param SizeType numeric type used for index into diff_result, for non-unique Containers a larger type may be required
 * @param Container container type for ordered diff and for diff_result
 */
template <typename T, typename SizeType = size_t, template<typename Y, typename...> typename Container = std::vector>
requires std::equality_comparable<T> && std::random_access_iterator<typename Container<T>::iterator>
class ordered_diff {
public:
   struct diff_result {
      Container<SizeType>                remove_indexes;
      Container<std::pair<SizeType, T>>  insert_indexes;
   };

   /// Generate diff_result that when `apply_diff(source, diff_result)` will modify source to be equal to target.
   static diff_result diff(const Container<T>& source, const Container<T>& target) {
      size_t s = 0;
      size_t t = 0;

      diff_result result;
      while (s < source.size() || t < target.size()) {
         FC_ASSERT(s < std::numeric_limits<SizeType>::max());
         FC_ASSERT(t < std::numeric_limits<SizeType>::max());
         if (s < source.size() && t < target.size()) {
            if (source[s] == target[t]) {
               // nothing to do, skip over
               ++s;
               ++t;
            } else { // not equal
               if (s == source.size() - 1 && t == target.size() - 1) {
                  FC_ASSERT(result.remove_indexes.size() < MAX_NUM_ARRAY_ELEMENTS);
                  FC_ASSERT(result.insert_indexes.size() < MAX_NUM_ARRAY_ELEMENTS);
                  // both at end, insert target and remove source
                  result.remove_indexes.push_back(s);
                  result.insert_indexes.emplace_back(t, target[t]);
                  ++s;
                  ++t;
               } else if (s + 1 < source.size() && t + 1 < target.size() && source[s + 1] == target[t + 1]) {
                  FC_ASSERT(result.remove_indexes.size() < MAX_NUM_ARRAY_ELEMENTS);
                  FC_ASSERT(result.insert_indexes.size() < MAX_NUM_ARRAY_ELEMENTS);
                  // misalignment, but next value equal, insert and remove
                  result.remove_indexes.push_back(s);
                  result.insert_indexes.emplace_back(t, target[t]);
                  ++s;
                  ++t;
               } else if (t + 1 < target.size() && source[s] == target[t + 1]) {
                  // source equals next target, insert current target
                  FC_ASSERT(result.insert_indexes.size() < MAX_NUM_ARRAY_ELEMENTS);
                  result.insert_indexes.emplace_back(t, target[t]);
                  ++t;
               } else { // source[s + 1] == target[t]
                  // target matches next source, remove current source
                  FC_ASSERT(result.remove_indexes.size() < MAX_NUM_ARRAY_ELEMENTS);
                  result.remove_indexes.push_back(s);
                  ++s;
               }
            }
         } else if (s < source.size()) {
            // remove extra in source
            FC_ASSERT(result.remove_indexes.size() < MAX_NUM_ARRAY_ELEMENTS);
            result.remove_indexes.push_back(s);
            ++s;
         } else if (t < target.size()) {
            // insert extra in target
            FC_ASSERT(result.insert_indexes.size() < MAX_NUM_ARRAY_ELEMENTS);
            result.insert_indexes.emplace_back(t, target[t]);
            ++t;
         }
      }

      return result;
   }

   /// @param diff the diff_result created from diff(source, target), apply_diff(std::move(source), diff_result) => target
   /// @param container the source of diff(source, target) to modify using the diff_result to produce original target
   /// @return the modified container now equal to original target
   template <typename X>
   requires std::same_as<std::decay_t<X>, diff_result>
   static Container<T> apply_diff(Container<T>&& container, X&& diff) {
      FC_ASSERT(diff.remove_indexes.size() <= MAX_NUM_ARRAY_ELEMENTS);
      FC_ASSERT(diff.insert_indexes.size() <= MAX_NUM_ARRAY_ELEMENTS);

      // Remove from the source based on diff.remove_indexes
      std::ptrdiff_t offset = 0;
      for (SizeType index : diff.remove_indexes) {
         FC_ASSERT(index + offset < container.size(), "diff.remove_indexes index ${idx} + offset ${o} not in range ${s}",
                   ("idx", index)("o", offset)("s", container.size()));
         container.erase(container.begin() + index + offset);
         --offset;
      }

      // Insert into the source based on diff.insert_indexes
      for (auto& [index, value] : diff.insert_indexes) {
         FC_ASSERT(index <= container.size(), "diff.insert_indexes index ${idx} not in range ${s}",
                   ("idx", index)("s", container.size()));
         FC_ASSERT(container.size() < MAX_NUM_ARRAY_ELEMENTS);
         container.insert(container.begin() + index, std::move(value));
      }
      return container;
   }

}; // class ordered_diff

} // namespace fc
