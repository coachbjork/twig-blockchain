#include <eosio/chain/webassembly/interface.hpp>
#include <eosio/chain/protocol_state_object.hpp>
#include <eosio/chain/transaction_context.hpp>
#include <eosio/chain/apply_context.hpp>
#include <fc/io/datastream.hpp>
#include <fc/crypto/modular_arithmetic.hpp>
#include <fc/crypto/blake2.hpp>
#include <fc/crypto/sha3.hpp>
#include <fc/crypto/k1_recover.hpp>
#undef g1_add
#include <fc/crypto/bls_utils.hpp>
#include <bn256/bn256.h>
#include <bls12-381/bls12-381.hpp>

namespace {
    uint32_t ceil_log2(uint32_t n)
    {
        if (n <= 1) {
            return 0;
        }
        return 32 - __builtin_clz(n - 1);
    };
}

namespace eosio { namespace chain { namespace webassembly {

   void interface::assert_recover_key( legacy_ptr<const fc::sha256> digest,
                                       legacy_span<const char> sig,
                                       legacy_span<const char> pub ) const {
      fc::crypto::signature s;
      fc::crypto::public_key p;
      fc::datastream<const char*> ds( sig.data(), sig.size() );
      fc::datastream<const char*> pubds ( pub.data(), pub.size() );

      fc::raw::unpack( ds, s );
      fc::raw::unpack( pubds, p );

      EOS_ASSERT(s.which() < context.db.get<protocol_state_object>().num_supported_key_types, unactivated_signature_type,
        "Unactivated signature type used during assert_recover_key");
      EOS_ASSERT(p.which() < context.db.get<protocol_state_object>().num_supported_key_types, unactivated_key_type,
        "Unactivated key type used when creating assert_recover_key");

      if(context.control.is_speculative_block())
         EOS_ASSERT(s.variable_size() <= context.control.configured_subjective_signature_length_limit(),
                    sig_variable_size_limit_exception, "signature variable length component size greater than subjective maximum");

      auto check = fc::crypto::public_key( s, *digest, false );
      EOS_ASSERT( check == p, crypto_api_exception, "Error expected key different than recovered key" );
   }

   int32_t interface::recover_key( legacy_ptr<const fc::sha256> digest,
                                   legacy_span<const char> sig,
                                   legacy_span<char> pub ) const {
      fc::crypto::signature s;
      fc::datastream<const char*> ds( sig.data(), sig.size() );
      fc::raw::unpack(ds, s);

      EOS_ASSERT(s.which() < context.db.get<protocol_state_object>().num_supported_key_types, unactivated_signature_type,
                 "Unactivated signature type used during recover_key");

      if(context.control.is_speculative_block())
         EOS_ASSERT(s.variable_size() <= context.control.configured_subjective_signature_length_limit(),
                    sig_variable_size_limit_exception, "signature variable length component size greater than subjective maximum");


      auto recovered = fc::crypto::public_key(s, *digest, false);

      // the key types newer than the first 2 may be varible in length
      if (s.which() >= config::genesis_num_supported_key_types ) {
         EOS_ASSERT(pub.size() >= 33, wasm_execution_error,
                    "destination buffer must at least be able to hold an ECC public key");
         auto packed_pubkey = fc::raw::pack(recovered);
         auto copy_size = std::min<size_t>(pub.size(), packed_pubkey.size());
         std::memcpy(pub.data(), packed_pubkey.data(), copy_size);
         return packed_pubkey.size();
      } else {
         // legacy behavior, key types 0 and 1 always pack to 33 bytes.
         // this will do one less copy for those keys while maintaining the rules of
         //    [0..33) dest sizes: assert (asserts in fc::raw::pack)
         //    [33..inf) dest sizes: return packed size (always 33)
         fc::datastream<char*> out_ds( pub.data(), pub.size() );
         fc::raw::pack(out_ds, recovered);
         return out_ds.tellp();
      }
   }

   void interface::assert_sha256(legacy_span<const char> data, legacy_ptr<const fc::sha256> hash_val) const {
      auto result = context.trx_context.hash_with_checktime<fc::sha256>( data.data(), data.size() );
      EOS_ASSERT( result == *hash_val, crypto_api_exception, "hash mismatch" );
   }

   void interface::assert_sha1(legacy_span<const char> data, legacy_ptr<const fc::sha1> hash_val) const {
      auto result = context.trx_context.hash_with_checktime<fc::sha1>( data.data(), data.size() );
      EOS_ASSERT( result == *hash_val, crypto_api_exception, "hash mismatch" );
   }

   void interface::assert_sha512(legacy_span<const char> data, legacy_ptr<const fc::sha512> hash_val) const {
      auto result = context.trx_context.hash_with_checktime<fc::sha512>( data.data(), data.size() );
      EOS_ASSERT( result == *hash_val, crypto_api_exception, "hash mismatch" );
   }

   void interface::assert_ripemd160(legacy_span<const char> data, legacy_ptr<const fc::ripemd160> hash_val) const {
      auto result = context.trx_context.hash_with_checktime<fc::ripemd160>( data.data(), data.size() );
      EOS_ASSERT( result == *hash_val, crypto_api_exception, "hash mismatch" );
   }

   void interface::sha1(legacy_span<const char> data, legacy_ptr<fc::sha1> hash_val) const {
      *hash_val = context.trx_context.hash_with_checktime<fc::sha1>( data.data(), data.size() );
   }

   void interface::sha256(legacy_span<const char> data, legacy_ptr<fc::sha256> hash_val) const {
      *hash_val = context.trx_context.hash_with_checktime<fc::sha256>( data.data(), data.size() );
   }

   void interface::sha512(legacy_span<const char> data, legacy_ptr<fc::sha512> hash_val) const {
      *hash_val = context.trx_context.hash_with_checktime<fc::sha512>( data.data(), data.size() );
   }

   void interface::ripemd160(legacy_span<const char> data, legacy_ptr<fc::ripemd160> hash_val) const {
      *hash_val = context.trx_context.hash_with_checktime<fc::ripemd160>( data.data(), data.size() );
   }

#undef g1_add

   int32_t interface::alt_bn128_add(span<const char> op1, span<const char> op2, span<char> result ) const {
      if (op1.size() != 64 ||  op2.size() != 64 ||  result.size() < 64 ||
         bn256::g1_add(std::span<const uint8_t, 64>{(const uint8_t*)op1.data(), 64},
                       std::span<const uint8_t, 64>{(const uint8_t*)op2.data(), 64},
                       std::span<uint8_t, 64>{ (uint8_t*)result.data(), 64}) == -1)
         return return_code::failure;
      return return_code::success;
   }

   int32_t interface::alt_bn128_mul(span<const char> g1_point, span<const char> scalar, span<char> result) const {
      if (g1_point.size() != 64 ||  scalar.size() != 32 ||  result.size() < 64 ||
         bn256::g1_scalar_mul(std::span<const uint8_t, 64>{(const uint8_t*)g1_point.data(), 64},
                              std::span<const uint8_t, 32>{(const uint8_t*)scalar.data(), 32},
                              std::span<uint8_t, 64>{ (uint8_t*)result.data(), 64}) == -1)
         return return_code::failure;
      return return_code::success;
   }

   int32_t interface::alt_bn128_pair(span<const char> g1_g2_pairs) const {
      auto checktime = [this]() { context.trx_context.checktime(); };
      auto res = bn256::pairing_check({(const uint8_t*)g1_g2_pairs.data(), g1_g2_pairs.size()} , checktime);
      if (res == -1)
         return return_code::failure;
      else
         return res? 0 : 1;
   }

   int32_t interface::mod_exp(span<const char> base,
                              span<const char> exp,
                              span<const char> modulus,
                              span<char> out) const {
      if (context.control.is_speculative_block()) {
         unsigned int base_modulus_size = std::max(base.size(), modulus.size());

         if (base_modulus_size < exp.size()) {
            EOS_THROW(subjective_block_production_exception,
                      "mod_exp restriction: exponent bit size cannot exceed bit size of either base or modulus");
         }

         static constexpr uint64_t bit_calc_limit = 106;

         uint64_t bit_calc = 5 * ceil_log2(exp.size()) + 8 * ceil_log2(base_modulus_size);

         if (bit_calc_limit < bit_calc) {
            EOS_THROW(subjective_block_production_exception,
                      "mod_exp restriction: bit size too large for input arguments");
         }
      }

      bytes bbase(base.data(), base.data() + base.size());
      bytes bexp(exp.data(), exp.data() + exp.size());
      bytes bmod(modulus.data(), modulus.data() + modulus.size());

      auto maybe_err = fc::modexp(bbase, bexp, bmod);
      if(std::holds_alternative<fc::modular_arithmetic_error>(maybe_err)) {
         return return_code::failure;
      }

      const auto& res = std::get<bytes>(maybe_err);

      if( out.size() < res.size() )
         return return_code::failure;

      std::memcpy( out.data(), res.data(), res.size() );
      return return_code::success;
   }

   int32_t interface::blake2_f( uint32_t rounds,
                                span<const char> state,
                                span<const char> message,
                                span<const char> t0_offset,
                                span<const char> t1_offset,
                                int32_t final,
                                span<char> out) const {

      bool _final = final == 1;
      bytes bstate(state.data(), state.data() + state.size());
      bytes bmessage(message.data(), message.data() + message.size());
      bytes bt0_offset(t0_offset.data(), t0_offset.data() + t0_offset.size());
      bytes bt1_offset(t1_offset.data(), t1_offset.data() + t1_offset.size());

      auto checktime = [this]() { context.trx_context.checktime(); };

      auto maybe_err = fc::blake2b(rounds, bstate, bmessage, bt0_offset, bt1_offset, _final, checktime);
      if(std::holds_alternative<fc::blake2b_error>(maybe_err)) {
         return return_code::failure;
      }

      const auto& res = std::get<bytes>(maybe_err);

      if( out.size() < res.size() )
         return return_code::failure;

      std::memcpy( out.data(), res.data(), res.size() );
      return return_code::success;
   }

   void interface::sha3( span<const char> input, span<char> output, int32_t keccak ) const {
      bool _keccak = keccak == 1;
      const size_t bs = eosio::chain::config::hashing_checktime_block_size;
      const char* data = input.data();
      uint32_t datalen = input.size();
      fc::sha3::encoder enc;
      while ( datalen > bs ) {
         enc.write( data, bs);
         data    += bs;
         datalen -= bs;
         context.trx_context.checktime();
      }
      enc.write( data, datalen);
      auto res = enc.result(!_keccak);

      auto copy_size = std::min( output.size(), res.data_size() );
      std::memcpy( output.data(), res.data(), copy_size );
   }

   int32_t interface::k1_recover( span<const char> signature, span<const char> digest, span<char> pub) const {
      bytes bsignature(signature.data(), signature.data() + signature.size());
      bytes bdigest(digest.data(), digest.data() + digest.size());

      auto maybe_err = fc::k1_recover(bsignature, bdigest);
      if( std::holds_alternative<fc::k1_recover_error>(maybe_err)) {
         return return_code::failure;
      }

      const auto& res = std::get<bytes>(maybe_err);

      if( pub.size() < res.size() )
         return return_code::failure;

      std::memcpy( pub.data(), res.data(), res.size() );
      return return_code::success;
   }

   int32_t interface::bls_g1_add(span<const char> op1, span<const char> op2, span<char> result) const
   {
      if(op1.size() != 144 ||  op2.size() != 144 ||  result.size() != 144)
         return return_code::failure;
      std::optional<bls12_381::g1> a = bls12_381::g1::fromJacobianBytesLE(std::span<const uint8_t, 144>((const uint8_t*)op1.data(), 144), false, true);
      std::optional<bls12_381::g1> b = bls12_381::g1::fromJacobianBytesLE(std::span<const uint8_t, 144>((const uint8_t*)op2.data(), 144), false, true);
      if(!a.has_value() || !b.has_value())
         return return_code::failure;
      bls12_381::g1 c = a.value().add(b.value());
      c.toJacobianBytesLE(std::span<uint8_t, 144>((uint8_t*)result.data(), 144), true);
      return return_code::success;
   }

   int32_t interface::bls_g2_add(span<const char> op1, span<const char> op2, span<char> result) const
   {
      if(op1.size() != 288 ||  op2.size() != 288 ||  result.size() != 288)
         return return_code::failure;
      std::optional<bls12_381::g2> a = bls12_381::g2::fromJacobianBytesLE(std::span<const uint8_t, 288>((const uint8_t*)op1.data(), 288), false, true);
      std::optional<bls12_381::g2> b = bls12_381::g2::fromJacobianBytesLE(std::span<const uint8_t, 288>((const uint8_t*)op2.data(), 288), false, true);
      if(!a.has_value() || !b.has_value())
         return return_code::failure;
      bls12_381::g2 c = a.value().add(b.value());
      c.toJacobianBytesLE(std::span<uint8_t, 288>((uint8_t*)result.data(), 288), true);
      return return_code::success;
   }

   int32_t interface::bls_g1_mul(span<const char> point, span<const char> scalar, span<char> result) const
   {
      if(point.size() != 144 ||  scalar.size() != 32 ||  result.size() != 144)
         return return_code::failure;
      std::optional<bls12_381::g1> a = bls12_381::g1::fromJacobianBytesLE(std::span<const uint8_t, 144>((const uint8_t*)point.data(), 144), false, true);
      if(!a.has_value())
         return return_code::failure;
      std::array<uint64_t, 4> b = bls12_381::scalar::fromBytesLE<4>(std::span<uint8_t, 32>((uint8_t*)scalar.data(), 32));
      bls12_381::g1 c = a.value().mulScalar(b);
      c.toJacobianBytesLE(std::span<uint8_t, 144>((uint8_t*)result.data(), 144), true);
      return return_code::success;
   }

   int32_t interface::bls_g2_mul(span<const char> point, span<const char> scalar, span<char> result) const
   {
      if(point.size() != 288 ||  scalar.size() != 32 ||  result.size() != 288)
         return return_code::failure;
      std::optional<bls12_381::g2> a = bls12_381::g2::fromJacobianBytesLE(std::span<const uint8_t, 288>((const uint8_t*)point.data(), 288), false, true);
      if(!a.has_value())
         return return_code::failure;
      std::array<uint64_t, 4> b = bls12_381::scalar::fromBytesLE<4>(std::span<uint8_t, 32>((uint8_t*)scalar.data(), 32));
      bls12_381::g2 c = a.value().mulScalar(b);
      c.toJacobianBytesLE(std::span<uint8_t, 288>((uint8_t*)result.data(), 288), true);
      return return_code::success;
   }

   int32_t interface::bls_g1_exp(span<const char> points, span<const char> scalars, const uint32_t n, span<char> result) const
   {
      if(points.size() != n*144 ||  scalars.size() != n*32 ||  result.size() != 144)
         return return_code::failure;
      std::vector<bls12_381::g1> pv;
      std::vector<std::array<uint64_t, 4>> sv;
      pv.reserve(n);
      sv.reserve(n);
      for(uint32_t i = 0; i < n; i++)
      {
         std::optional<bls12_381::g1> p = bls12_381::g1::fromJacobianBytesLE(std::span<const uint8_t, 144>((const uint8_t*)points.data() + i*144, 144), false, true);
         if(!p.has_value())
            return return_code::failure;
         std::array<uint64_t, 4> s = bls12_381::scalar::fromBytesLE<4>(std::span<const uint8_t, 32>((const uint8_t*)scalars.data() + i*32, 32));
         pv.push_back(p.value());
         sv.push_back(s);
         if(i%10 == 0)
            context.trx_context.checktime();
      }
      bls12_381::g1 r = bls12_381::g1::multiExp(pv, sv, [this](){ context.trx_context.checktime(); }).value(); // accessing value is safe
      r.toJacobianBytesLE(std::span<uint8_t, 144>((uint8_t*)result.data(), 144), true);
      return return_code::success;
   }

   int32_t interface::bls_g2_exp(span<const char> points, span<const char> scalars, const uint32_t n, span<char> result) const
   {
      if(points.size() != n*288 ||  scalars.size() != n*32 ||  result.size() != 288)
         return return_code::failure;
      std::vector<bls12_381::g2> pv;
      std::vector<std::array<uint64_t, 4>> sv;
      pv.reserve(n);
      sv.reserve(n);
      for(uint32_t i = 0; i < n; i++)
      {
         std::optional<bls12_381::g2> p = bls12_381::g2::fromJacobianBytesLE(std::span<const uint8_t, 288>((const uint8_t*)points.data() + i*288, 288), false, true);
         if(!p.has_value())
            return return_code::failure;
         std::array<uint64_t, 4> s = bls12_381::scalar::fromBytesLE<4>(std::span<const uint8_t, 32>((const uint8_t*)scalars.data() + i*32, 32));
         pv.push_back(p.value());
         sv.push_back(s);
         if(i%6 == 0)
            context.trx_context.checktime();
      }
      bls12_381::g2 r = bls12_381::g2::multiExp(pv, sv, [this](){ context.trx_context.checktime(); }).value(); // accessing value is safe
      r.toJacobianBytesLE(std::span<uint8_t, 288>((uint8_t*)result.data(), 288), true);
      return return_code::success;
   }

   int32_t interface::bls_pairing(span<const char> g1_points, span<const char> g2_points, const uint32_t n, span<char> result) const
   {
      if(g1_points.size() != n*144 ||  g2_points.size() != n*288 ||  result.size() != 576)
         return return_code::failure;
      std::vector<std::tuple<bls12_381::g1, bls12_381::g2>> v;
      v.reserve(n);
      for(uint32_t i = 0; i < n; i++)
      {
         std::optional<bls12_381::g1> p_g1 = bls12_381::g1::fromJacobianBytesLE(std::span<const uint8_t, 144>((const uint8_t*)g1_points.data() + i*144, 144), true, true);
         std::optional<bls12_381::g2> p_g2 = bls12_381::g2::fromJacobianBytesLE(std::span<const uint8_t, 288>((const uint8_t*)g2_points.data() + i*288, 288), true, true);
         if(!p_g1.has_value() || !p_g2.has_value())
            return return_code::failure;
         bls12_381::pairing::add_pair(v, p_g1.value(), p_g2.value());
         if(i%4 == 0)
            context.trx_context.checktime();
      }
      bls12_381::fp12 r = bls12_381::pairing::calculate(v, [this](){ context.trx_context.checktime(); });
      r.toBytesLE(std::span<uint8_t, 576>((uint8_t*)result.data(), 576), true);
      return return_code::success;
   }

   int32_t interface::bls_g1_map(span<const char> e, span<char> result) const
   {
      if(e.size() != 48 ||  result.size() != 144)
         return return_code::failure;
      std::optional<bls12_381::fp> a = bls12_381::fp::fromBytesLE(std::span<const uint8_t, 48>((const uint8_t*)e.data(), 48), true, true);
      if(!a.has_value())
         return return_code::failure;
      bls12_381::g1 c = bls12_381::g1::mapToCurve(a.value());
      c.toJacobianBytesLE(std::span<uint8_t, 144>((uint8_t*)result.data(), 144), true);
      return return_code::success;
   }

   int32_t interface::bls_g2_map(span<const char> e, span<char> result) const
   {
      if(e.size() != 96 ||  result.size() != 288)
         return return_code::failure;
      std::optional<bls12_381::fp2> a = bls12_381::fp2::fromBytesLE(std::span<const uint8_t, 96>((const uint8_t*)e.data(), 96), true, true);
      if(!a.has_value())
         return return_code::failure;
      bls12_381::g2 c = bls12_381::g2::mapToCurve(a.value());
      c.toJacobianBytesLE(std::span<uint8_t, 288>((uint8_t*)result.data(), 288), true);
      return return_code::success;
   }

   int32_t interface::bls_fp_mod(span<const char> s, span<char> result) const
   {
      if(s.size() != 64 ||  result.size() != 48)
         return return_code::failure;
      std::array<uint64_t, 8> k = bls12_381::scalar::fromBytesLE<8>(std::span<const uint8_t, 64>((const uint8_t*)s.data(), 64));
      bls12_381::fp e = bls12_381::fp::modPrime<8>(k);
      e.toBytesLE(std::span<uint8_t, 48>((uint8_t*)result.data(), 48), true);
      return return_code::success;
   }

   bool interface::bls_verify( span<const char> signature, span<const char> digest, span<const char> pub) const {

      fc::crypto::blslib::bls_signature u_sig;
      fc::crypto::blslib::bls_public_key u_pub;
      vector<uint8_t> u_digest;

      fc::datastream<const char*> s_sig( signature.data(), signature.size() );
      fc::datastream<const char*> s_pub( pub.data(), pub.size() );
      fc::datastream<const char*> s_digest( digest.data(), digest.size() );

      fc::raw::unpack( s_sig, u_sig );
      fc::raw::unpack( s_pub, u_pub );
      fc::raw::unpack( s_digest, u_digest );

      std::cerr << u_pub.to_string() << "\n";
      std::cerr << u_sig.to_string() << "\n";

      bool result = fc::crypto::blslib::verify(u_pub, u_digest, u_sig);

      return result;

   }

   int32_t interface::bls_aggregate_pubkeys( span<const char> pubkeys, span<char> aggregate) const {

      vector<fc::crypto::blslib::bls_public_key> u_pubkeys;

      fc::datastream<const char*> s_pubkeys( pubkeys.data(), pubkeys.size() );

      fc::raw::unpack( s_pubkeys, u_pubkeys );

      fc::crypto::blslib::bls_public_key agg_pubkey = fc::crypto::blslib::aggregate(u_pubkeys);

      auto packed = fc::raw::pack(agg_pubkey);

      auto copy_size = std::min<size_t>(aggregate.size(), packed.size());

      std::memcpy(aggregate.data(), packed.data(), copy_size);

      return packed.size();

   }

   int32_t interface::bls_aggregate_sigs( span<const char> signatures, span<char> aggregate) const {

      vector<fc::crypto::blslib::bls_signature> u_sigs;

      fc::datastream<const char*> s_sigs( signatures.data(), signatures.size() );

      fc::raw::unpack( s_sigs, u_sigs );

      fc::crypto::blslib::bls_signature agg_sig = fc::crypto::blslib::aggregate(u_sigs);

      auto packed = fc::raw::pack(agg_sig);

      auto copy_size = std::min<size_t>(aggregate.size(), packed.size());

      std::memcpy(aggregate.data(), packed.data(), copy_size);

      return packed.size();

   }

   bool interface::bls_aggregate_verify( span<const char> signature, span<const char> digests, span<const char> pubs) const {

      fc::crypto::blslib::bls_signature u_sig;
      vector<fc::crypto::blslib::bls_public_key> u_pubs;
      vector<vector<uint8_t>> u_digests;

      fc::datastream<const char*> s_sig( signature.data(), signature.size() );
      fc::datastream<const char*> s_pubs( pubs.data(), pubs.size() );
      fc::datastream<const char*> s_digests( digests.data(), digests.size() );

      fc::raw::unpack( s_sig, u_sig );
      fc::raw::unpack( s_pubs, u_pubs );
      fc::raw::unpack( s_digests, u_digests );

      bool result = fc::crypto::blslib::aggregate_verify(u_pubs, u_digests, u_sig);

      return result;
   }

}}} // ns eosio::chain::webassembly
