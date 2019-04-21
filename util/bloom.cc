// Copyright (c) 2012 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "leveldb/filter_policy.h"

#include "leveldb/slice.h"
#include "util/hash.h"

namespace leveldb {

namespace {
static uint32_t BloomHash(const Slice& key) {
  return Hash(key.data(), key.size(), 0xbc9f1d34);
}

class BloomFilterPolicy : public FilterPolicy {
 private:
  size_t bits_per_key_;            // 平均一个key有多少个位  m/n的值
  size_t k_;                       // 哈希函数个数             数学问题

 public:
  explicit BloomFilterPolicy(int bits_per_key)
      : bits_per_key_(bits_per_key) {
    // We intentionally round down to reduce probing cost a little bit
    k_ = static_cast<size_t>(bits_per_key * 0.69);  // 0.69 =~ ln(2)     // bits_per_key
    if (k_ < 1) k_ = 1;
    if (k_ > 30) k_ = 30;
  }

  virtual const char* Name() const {
    return "leveldb.BuiltinBloomFilter2";
  }

  virtual void CreateFilter(const Slice* keys, int n, std::string* dst) const {
    // Compute bloom filter size (in both bits and bytes)
    size_t bits = n * bits_per_key_;                                   // 

    // For small n, we can see a very high false positive rate.  Fix it
    // by enforcing a minimum bloom filter length.
    if (bits < 64) bits = 64;

    size_t bytes = (bits + 7) / 8;                 // 计算出最少需要多少byte， 没有结果直接+1， 完美解决整除问题
    bits = bytes * 8;

    const size_t init_size = dst->size();
    dst->resize(init_size + bytes, 0);、
     // 记录hash的次数， 变量要保留, bits_per_key_不用保留的原因是其算出来的值已经保留在长度上，不用再单独保留
    dst->push_back(static_cast<char>(k_));  // Remember # of probes in filter 
    char* array = &(*dst)[init_size];

    for (int i = 0; i < n; i++) {
      // Use double-hashing to generate a sequence of hash values.
      // See analysis in [Kirsch,Mitzenmacher 2006].
      uint32_t h = BloomHash(keys[i]);        // h是hash的值
      const uint32_t delta = (h >> 17) | (h << 15);  // Rotate right 17 bits      反转

      // 使用k个哈希函数，计算出k位，每位都赋值为1。
      // 为了减少哈希冲突，减少误判。
      for (size_t j = 0; j < k_; j++) {                   //  k    hash的次数
        const uint32_t bitpos = h % bits;                 //  总的位数，
        array[bitpos/8] |= (1 << (bitpos % 8));           //  除 8 | h除8的位
        h += delta;                                       //  移动， 得到新的hash值
      }
    }
  }

  virtual bool KeyMayMatch(const Slice& key, const Slice& bloom_filter) const {
    const size_t len = bloom_filter.size();
    if (len < 2) return false;

    const char* array = bloom_filter.data();
    const size_t bits = (len - 1) * 8;

    // Use the encoded k so that we can read filters generated by
    // bloom filters created using different parameters.
    const size_t k = array[len-1];
    if (k > 30) {
      // Reserved for potentially new encodings for short bloom filters.
      // Consider it a match.
      return true;
    }

    uint32_t h = BloomHash(key);
    const uint32_t delta = (h >> 17) | (h << 15);  // Rotate right 17 bits
    for (size_t j = 0; j < k; j++) {
      const uint32_t bitpos = h % bits;
      if ((array[bitpos/8] & (1 << (bitpos % 8))) == 0) return false;
      h += delta;
    }
    return true;
  }
};
}

const FilterPolicy* NewBloomFilterPolicy(int bits_per_key) {
  return new BloomFilterPolicy(bits_per_key);
}

}  // namespace leveldb
